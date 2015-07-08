#include "KineticAutoConnection.hh"
#include "LoggingException.hh"
#include <sstream>

using namespace kinetic;
using namespace kio;

KineticAutoConnection::KineticAutoConnection(
        std::pair< kinetic::ConnectionOptions, kinetic::ConnectionOptions > o,
        std::chrono::seconds r) :
        connection(), options(o), timestamp(), ratelimit(r), 
        status(kinetic::StatusCode::CLIENT_INTERNAL_ERROR,""), mutex(new std::mutex())
{}

KineticAutoConnection::~KineticAutoConnection()
{
}

void KineticAutoConnection::setError(kinetic::KineticStatus s)
{
  std::lock_guard<std::mutex> lck(*mutex);
  status = s;
}

std::shared_ptr<kinetic::ThreadsafeNonblockingKineticConnection> KineticAutoConnection::get()
{
  std::lock_guard<std::mutex> lck(*mutex);

  if(!status.ok()){
    connect();
    if(!status.ok())
      throw LoggingException(ENXIO,__FUNCTION__,__FILE__,__LINE__,
              "Invalid connection: " + status.message());
  }
  return connection;
}

void KineticAutoConnection::connect()
{
  /* Rate limit connection attempts. */
  using std::chrono::system_clock;
  using std::chrono::duration_cast;
  using std::chrono::seconds;
  if(duration_cast<seconds>(system_clock::now() - timestamp) < ratelimit)
    return;

  /* Remember this reconnection attempt. */
  timestamp = system_clock::now();

  KineticConnectionFactory factory = NewKineticConnectionFactory();

  /* Attempt connection. Prioritize first address, but take second if first
   * failed. */
  if(factory.NewThreadsafeNonblockingConnection(options.first,  connection).ok() ||
     factory.NewThreadsafeNonblockingConnection(options.second, connection).ok()){
    status = KineticStatus(StatusCode::OK,"");
  }
  else{
    std::stringstream ss;
    ss << "Failed building connection to " << options.first.host << ":"
       << options.first.port << " and " << options.second.host << ":"
       << options.second.port;
    status = KineticStatus(StatusCode::REMOTE_REMOTE_CONNECTION_ERROR, ss.str());
  }
}