#include "KineticChunk.hh"
#include "KineticClusterInterface.hh"
#include "KineticException.hh"
#include <algorithm>
#include <errno.h>

using std::unique_ptr;
using std::shared_ptr;
using std::string;
using std::chrono::milliseconds;
using std::chrono::system_clock;
using std::chrono::duration_cast;
using kinetic::KineticStatus;
using kinetic::StatusCode;

const int KineticChunk::expiration_time = 1000;


KineticChunk::KineticChunk(std::shared_ptr<KineticClusterInterface> c,
    const std::shared_ptr<const std::string> k, bool skip_initial_get) :
        cluster(c), key(k), version(new string()), value(new string()),
        timestamp(), updates()
{
  if(skip_initial_get == false)
    getRemoteValue();
}

KineticChunk::~KineticChunk()
{
}

bool KineticChunk::validateVersion()
{
  /* See if check is unnecessary based on expiration. */
  if(duration_cast<milliseconds>(system_clock::now() - timestamp).count() < expiration_time)
    return true;

  /* Check remote version & compare it to in-memory version. */
  shared_ptr<const string> remote_version;
  shared_ptr<string> remote_value;
  KineticStatus status = cluster->get(key,remote_version,remote_value,true);

   /*If no version is set, the entry has never been flushed. In this case,
     not finding an entry with that key in the cluster is expected. */
  if( (version->empty() && status.statusCode() == StatusCode::REMOTE_NOT_FOUND) ||
      (status.ok() && remote_version && *version == *remote_version)
    ){
      /* In memory version equals remote version. Remember the time. */
      timestamp = system_clock::now();
      return true;
  }
  return false;
}

void KineticChunk::getRemoteValue()
{
  std::shared_ptr<string> rv;
  KineticStatus status = cluster->get(key, version, rv, false);

  if(!status.ok() && status.statusCode() != StatusCode::REMOTE_NOT_FOUND)
    throw KineticException(EIO,__FUNCTION__,__FILE__,__LINE__,
            "Attempting to read key '"+ *key+"' from cluster returned error "
            "message '"+status.message()+"'");

  /* We read in the current value from the drive. Remember the time. */
  timestamp = system_clock::now();

  /* Merge all updates done on the local data copy (data) into the freshly
     read-in data copy. */
  if(status.statusCode() == StatusCode::REMOTE_NOT_FOUND)
    rv.reset(new std::string());
  else
    rv->resize(std::max(rv->size(), value->size()));

  for (auto iter = updates.begin(); iter != updates.end(); ++iter){
    auto update = *iter;
    if(update.second)
      rv->replace(update.first, update.second, value->c_str(), update.first, update.second);
    else
      rv->resize(update.first);
  }

  /* The remote value with the merged in changes represents the up-to-date value
   * swap it in. */
  std::swap(rv, value);
}

void KineticChunk::read(char* const buffer, off_t offset, size_t length)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);

  if(buffer==NULL || offset<0 || offset+length > cluster->limits().max_value_size)
    throw KineticException(EINVAL,__FUNCTION__,__FILE__,__LINE__, "Invalid argument");

  /* Ensure data is not too stale to read. */
  if(!validateVersion())
    getRemoteValue();

  /* return 0s if client reads non-existing data (e.g. file with holes) */
  if(offset+length > value->size())
    memset(buffer,0,length);

  if(value->size()>(size_t)offset){
    size_t copy_length = std::min(length, (size_t)(value->size()-offset));
    value->copy(buffer, copy_length, offset);
  }
}

void KineticChunk::write(const char* const buffer, off_t offset, size_t length)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);

  if(buffer==NULL || offset<0 || offset+length>cluster->limits().max_value_size)
    throw KineticException(EINVAL,__FUNCTION__,__FILE__,__LINE__, "Invalid argument");

  /* Set new entry size. */
  value->resize(std::max((size_t) offset + length, value->size()));

  /* Copy data and remember write access. */
  value->replace(offset, length, buffer, length);
  updates.push_back(std::pair<off_t, size_t>(offset, length));
}

void KineticChunk::truncate(off_t offset)
{
  std::lock_guard<std::recursive_mutex> lock(mutex);

  if(offset<0 || offset>cluster->limits().max_value_size)
    throw KineticException(EINVAL,__FUNCTION__,__FILE__,__LINE__, "Invalid argument");

  value->resize(offset);
  updates.push_back(std::pair<off_t, size_t>(offset, 0));
}

void KineticChunk::flush()
{
  std::lock_guard<std::recursive_mutex> lock(mutex);

  /* only flush a chunk if it is dirty */
  if(!dirty())
    return;

  KineticStatus status = cluster->put(key,version,value,false);
  while(status.statusCode() == StatusCode::REMOTE_VERSION_MISMATCH){
    getRemoteValue();
    status = cluster->put(key,version,value,false);
  }

  if (!status.ok())
    throw KineticException(EIO,__FUNCTION__,__FILE__,__LINE__,
        "Attempting to write key '"+ *key+"' to the cluster returned error "
        "message '"+status.message()+"'");

  /* Success... we can forget about in-memory changes and set timestamp
     to current time. */
  updates.clear();
  timestamp = system_clock::now();
}

bool KineticChunk::dirty() const
{
  if(version->empty())
    return true;
  return !updates.empty();
}

int KineticChunk::size()
{
  std::lock_guard<std::recursive_mutex> lock(mutex);

  /* Ensure size is not too stale. */
  if(!validateVersion())
    getRemoteValue();

  return value->size();
}