//------------------------------------------------------------------------------
//! @file BackgroundOperationHandler.hh
//! @author Paul Hermann Lensing
//! @brief Execute background operations, spawning a limited number of concurrent threads.
//------------------------------------------------------------------------------

/************************************************************************
 * KineticIo - a file io interface library to kinetic devices.          *
 *                                                                      *
 * This Source Code Form is subject to the terms of the Mozilla         *
 * Public License, v. 2.0. If a copy of the MPL was not                 *
 * distributed with this file, You can obtain one at                    *
 * https://mozilla.org/MP:/2.0/.                                        *
 *                                                                      *
 * This program is distributed in the hope that it will be useful,      *
 * but is provided AS-IS, WITHOUT ANY WARRANTY; including without       *
 * the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or         *
 * FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public             *
 * License for more details.                                            *
 ************************************************************************/

#ifndef KINETICIO_BACKGROUNDOPERATIONHANDLER_HH
#define KINETICIO_BACKGROUNDOPERATIONHANDLER_HH

/* <cstdatomic> is part of gcc 4.4.x experimental C++0x support... <atomic> is
 * what actually made it into the standard.*/
#if __GNUC__ == 4 && (__GNUC_MINOR__ == 4)
    #include <cstdatomic>
#else
  #include <atomic>
#endif
#include <functional>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace kio {

//------------------------------------------------------------------------------
//! Execute a supplied function asynchronously in a different thread while
//! controlling maximum concurrency.
//------------------------------------------------------------------------------
class BackgroundOperationHandler {
public:
  //--------------------------------------------------------------------------
  //! If queue capacity is set to zero, run_noqueue will be called.
  //! Execute supplied function asynchronously. If queue capacity is breached,
  //! the calling thread will be blocked until queue shrinks below capacity.
  //!
  //! @param function the function to be executed.
  //--------------------------------------------------------------------------
  void run(std::function<void()>&& function);

  //--------------------------------------------------------------------------
  //! If queue capacity is set to zero, try_run_noqueue will be called.
  //! If queue capacity is reached, function will not be executed.
  //! Otherwise it will be queued for asynchronous execution.
  //!
  //! @param function the function to be executed.
  //! @return true if function is queued for execution, false otherwise
  //--------------------------------------------------------------------------
  bool try_run(std::function<void()>&& function);

  //--------------------------------------------------------------------------
  //! Change configuration during runtime.
  //!
  //! @param worker_threads maximum number of spawned background threads
  //! @param queue_depth maximum number of functions queued for execution
  //--------------------------------------------------------------------------
  void changeConfiguration(size_t worker_threads, size_t queue_depth);

  //--------------------------------------------------------------------------
  //! Constructor. Note that if queue_depth is set to zero, background threads
  //! will be spawned on demand instead of being managed in a thread-pool.
  //!
  //! @param worker_threads maximum number of spawned background threads
  //! @param queue_depth maximum number of functions queued for execution
  //--------------------------------------------------------------------------
  explicit BackgroundOperationHandler(size_t worker_threads, size_t queue_depth);

  //--------------------------------------------------------------------------
  //! Destructor
  //--------------------------------------------------------------------------
  ~BackgroundOperationHandler();

private:
  //--------------------------------------------------------------------------
  //! Threadsafe wrapper executing queued functions and counting thread use
  //--------------------------------------------------------------------------
  void worker_thread();

  //--------------------------------------------------------------------------
  //! Threadsafe wrapper executing supplied function and counting thread use
  //--------------------------------------------------------------------------
  void execute_noqueue(std::function<void()> function);

  //--------------------------------------------------------------------------
  //! If threadlimit is not reached, a new thread will be spawned to execute
  //! the supplied function. Otherwise the supplied function will be executed
  //! in the _calling_ thread.
  //!
  //! @param function the function to be executed.
  //--------------------------------------------------------------------------
  void run_noqueue(std::function<void()> function);

  //--------------------------------------------------------------------------
  //! If threadlimit is not reached, a new thread will be spawned to execute
  //! the supplied function. Otherwise the function is not executed.
  //!
  //! @param function the function to be executed.
  //! @return true if function is executed, false otherwise
  //--------------------------------------------------------------------------
  bool try_run_noqueue(std::function<void()> function);

private:
  //! queue of functions to be executed
  std::queue<std::function<void()>> q;
  //! maximum number of queue entries, atomic to support changeConfiguration
  std::atomic<size_t> queue_capacity;
  //! maximum number of background threads, atomic to support changeConfiguration
  std::atomic<size_t> thread_capacity;
  //! concurrency control for queue access;
  std::mutex queue_mutex;
  //! workers block until an item is inserted into queue
  std::condition_variable worker;
  //! controller will be triggered when an item is removed from queue
  std::condition_variable controller;
  //! current number of active background threads
  std::atomic<size_t> numthreads;
  //! signal worker threads to shutdown
  std::atomic<bool> shutdown;
};

}

#endif //KINETICIO_BACKGROUNDOPERATIONHANDLER_HH
