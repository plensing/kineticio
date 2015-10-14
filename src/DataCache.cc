#include "DataCache.hh"
#include "FileIo.hh"
#include "Utility.hh"
#include "Logging.hh"
#include <thread>
#include <unistd.h>

using namespace kio;


DataCache::DataCache(size_t preferred_size, size_t capacity, size_t bg_threads,
                                     size_t bg_queue_depth, size_t readahead_size) :
    target_size(preferred_size), capacity(capacity), current_size(0), bg(bg_threads, bg_queue_depth),
    readahead_window_size(readahead_size), tail_items(0)
{
  if (capacity < target_size) throw std::logic_error("cache target size may not exceed capacity");
  if (bg_threads < 0) throw std::logic_error("number of background threads cannot be negative.");
}

void DataCache::changeConfiguration(size_t preferred_size, size_t cap, size_t bg_threads,
                                            size_t bg_queue_depth, size_t readahead_size)
{
  {
    std::lock_guard<std::mutex> lock(readahead_mutex);  
    readahead_window_size = readahead_size;
  }
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    tail_items = 0;
  }
  target_size = preferred_size;
  capacity = cap;
  bg.changeConfiguration(bg_threads, bg_queue_depth);
}

void DataCache::drop(kio::FileIo* owner)
{
  /* If we encountered a exception in a background flush, we don't care
     about it if we are dropping the data anyways. */
  { std::lock_guard<std::mutex> lock(exception_mutex);
    if (exceptions.count(owner))
      exceptions.erase(owner);
  }
  { std::lock_guard<std::mutex> lock(readahead_mutex);
    if (prefetch.count(owner))
      prefetch.erase(owner);
  }

  std::lock_guard<std::mutex> lock(cache_mutex);
  if(owner_tables.count(owner)) {
    for (auto owit = owner_tables[owner].cbegin(); owit != owner_tables[owner].cend(); owit++) {
      cache_iterator it = *owit;
      it->owners.erase(owner);
      /* Only remove item from cache if there are no other owners left */
      if(!it->owners.size())
        remove_item(it);
    }
  }
  owner_tables.erase(owner);
}

void DataCache::flush(kio::FileIo* owner)
{
  /* If we encountered a exception in a background flush, we don't care
     about it, if it is still an issue we will re-encounter it during the flush operation. */
  { std::lock_guard<std::mutex> lock(exception_mutex);
    if (exceptions.count(owner))
      exceptions.erase(owner);
  }

  /* build a vector of bocks, so we can flush without holding cache_mutex */
  std::vector<std::shared_ptr<kio::DataBlock> > blocks;
  { std::lock_guard<std::mutex> lock(cache_mutex);
    if (owner_tables.count(owner)) {
      for (auto item = owner_tables[owner].cbegin(); item != owner_tables[owner].cend(); item++) {
        cache_iterator it = *item;
        blocks.push_back(it->data);
      }
    }
  }

  for (auto it = blocks.begin(); it != blocks.end(); it++) {
    auto& block = *it;
    if (block->dirty()) {
      block->flush();
    }
  }
}

DataCache::cache_iterator DataCache::remove_item(const cache_iterator& it)
{
  for(auto o = it->owners.cbegin(); o!= it->owners.cend(); o++)
    owner_tables[*o].erase(it);
  current_size -= it->data->capacity();
  lookup.erase(*it->data->getKey());
  return cache.erase(it);
}

void DataCache::throttle()
{
  using namespace std::chrono;
  static const milliseconds ratelimit(50);

  for(auto wait_pressure = 0.1; true; wait_pressure += 0.01) {
    {
      std::lock_guard<std::mutex> ratelimit_lock(cache_cleanup_mutex);
      if(duration_cast<milliseconds>(system_clock::now() - cache_cleanup_timestamp) > ratelimit){
        cache_cleanup_timestamp = system_clock::now();

        std::lock_guard<std::mutex> cachelock(cache_mutex);
        
        /* Attempt to free items from the tail of the cache if size > target_size. Don't access list::size() every time, 
           as it has linear runtime with gcc */
        if(!tail_items && current_size > target_size)            
            tail_items = cache.size() * 0.25; 
        int checked_items = 0;
        for (auto it = --cache.end(); current_size > target_size && it != cache.begin() && checked_items<tail_items; it--, checked_items++) {
          if (!it->data->dirty())
            it = remove_item(it);
        }
      }
    }

    if(cache_pressure() <= wait_pressure)
      break;

    /* Sleep 100 ms to give dirty data a chance to flush before retrying */
    usleep(1000 * 100);
  }
}

std::shared_ptr<kio::DataBlock> DataCache::get(
    kio::FileIo* owner, int blocknumber, DataBlock::Mode mode, RequestMode rm)
{
  { std::lock_guard<std::mutex> exeptionlock(exception_mutex);
    if (exceptions.count(owner)) {
      std::exception e = exceptions[owner];
      exceptions.erase(owner);
      throw e;
    }
  }

  /* If we are called by a client of the cache */
  if(rm == RequestMode::STANDARD){
    /* Register requested block with readahead logic unless we are opening the block for create */
    if (mode != DataBlock::Mode::CREATE)
      readahead(owner, blocknumber);
    /* Throttle this request as indicated by cache pressure */
    throttle();
  }

  std::lock_guard<std::mutex> cachelock(cache_mutex);
  auto key = utility::constructBlockKey(owner->block_basename, blocknumber);

  /* If the requested block is already cached, we can return it without IO. */
  if (lookup.count(*key)) {
    /* Splicing the element into the front of the list will keep iterators valid. */
    cache.splice(cache.begin(), cache, lookup[*key]);

    /* set owner<->cache_item relationship. Since we have std::sets there's no need to test for existence */
    owner_tables[owner].insert(cache.begin());
    cache.front().owners.insert(owner);
    return cache.front().data;
  }
  
  /* Attempt to free items from the tail of the cache if size > target_size. Don't access list::size() every time, 
          as it has linear runtime with gcc */
  if(!tail_items && current_size > target_size)         
    tail_items = cache.size() * 0.25; 
  int checked_items = 0;
  for (auto it = --cache.end(); current_size > target_size && it != cache.begin() && checked_items<tail_items; it--, checked_items++) {
    if (!it->data->dirty())
      it = remove_item(it);
  }

  /* If cache size would exceed capacity, we have to try flushing dirty blocks manually. */
  if (capacity < current_size + owner->cluster->limits().max_value_size) {
    kio_notice("Cache capacity reached.");
    auto& it = --cache.end();
    if (it->data->dirty()) {
      try {
        it->data->flush();
      }
      catch (const std::exception& e) {
        throw kio_exception(EIO, "Failed freeing cache space: ", e.what());
      }
    }
    remove_item(it);
  }

  auto data = std::make_shared<DataBlock>(
      owner->cluster,
      utility::constructBlockKey(owner->block_basename, blocknumber),
      mode
  );
  cache.push_front(CacheItem{std::set<kio:: FileIo*>{owner},data});
  lookup[*key] = cache.begin();
  current_size += data->capacity();

  /* set iterator in owner_tables */
  owner_tables[owner].insert(cache.begin());
  return data;
}

double DataCache::cache_pressure()
{
  if (current_size <= target_size) {
    return 0.0;
  }
  return (current_size - target_size) / static_cast<double>(capacity - target_size);
}

void DataCache::do_flush(kio::FileIo* owner, std::shared_ptr<kio::DataBlock> data)
{
  if (data->dirty()) {
    try {
      data->flush();
    }
    catch (const std::exception& e) {
      std::lock_guard<std::mutex> lock(exception_mutex);
      exceptions[owner] = e;
    }
  }
}

void DataCache::async_flush(kio::FileIo* owner, std::shared_ptr<DataBlock> data)
{
  bg.run(std::bind(&DataCache::do_flush, this, owner, data));
}

void do_readahead(std::shared_ptr<kio::DataBlock> data)
{
  /* if readahaed should throw, there's no need to remember as in do_flush...  we'll just re-encounter the exception
   * should the block actually be read from */
  char buf[1];
  data->read(buf, 0, 1);
}

void DataCache::readahead(kio::FileIo* owner, int blocknumber)
{
  std::list<int> prediction;
  { std::lock_guard<std::mutex> readaheadlock(readahead_mutex);
    if(!prefetch.count(owner))
      prefetch[owner] = SequencePatternRecognition(readahead_window_size);
    auto& sequence = prefetch[owner];
    sequence.add(blocknumber);
    /* Don't do readahead if cache is already under pressure. */
    if (cache_pressure() < 0.1)
      prediction = sequence.predict(SequencePatternRecognition::PredictionType::CONTINUE);
  }
  for (auto it = prediction.cbegin(); it != prediction.cend(); it++) {
    auto data = get(owner, *it, DataBlock::Mode::STANDARD, RequestMode::READAHEAD);
    bg.try_run(std::bind(do_readahead, data));
  }
}


