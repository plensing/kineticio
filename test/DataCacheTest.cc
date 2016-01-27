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

#include "catch.hpp"
#include "FileIo.hh"
#include "DataCache.hh"
#include <kio/FileIoInterface.hh>
#include "ClusterInterface.hh"
#include "Utility.hh"
#include "SimulatorController.h"
#include <chrono>
#include <unistd.h>


using namespace kio;
using namespace kinetic;
using namespace std::chrono;

class MockCluster : public ClusterInterface {
public:
  const std::string& instanceId() const
  {
    return _id;
  }

  const std::string& id() const
  {
    return _id;
  }

  const ClusterLimits& limits(KeyType type) const
  {
    return _limits;
  };

  ClusterStats stats()
  {
    return _stats;
  }

  kinetic::KineticStatus get(
      const std::shared_ptr<const std::string>& key,
      std::shared_ptr<const std::string>& version,
      std::shared_ptr<const std::string>& value,
      KeyType type)
  {
    version = _version;
    value = _value;
    return KineticStatus(StatusCode::OK, "");
  }

  kinetic::KineticStatus get(
      const std::shared_ptr<const std::string>& key,
      std::shared_ptr<const std::string>& version,
      KeyType type)
  {
    version = _version;
    return KineticStatus(StatusCode::OK, "");
  }

  kinetic::KineticStatus put(
      const std::shared_ptr<const std::string>& key,
      const std::shared_ptr<const std::string>& version,
      const std::shared_ptr<const std::string>& value,
      std::shared_ptr<const std::string>& version_out,
      KeyType type)
  {
    version_out = _version;
    return KineticStatus(StatusCode::OK, "");
  }

  kinetic::KineticStatus put(
      const std::shared_ptr<const std::string>& key,
      const std::shared_ptr<const std::string>& value,
      std::shared_ptr<const std::string>& version_out,
      KeyType type)
  {
    version_out = _version;
    return KineticStatus(StatusCode::OK, "");
  }

  kinetic::KineticStatus remove(
      const std::shared_ptr<const std::string>& key,
      const std::shared_ptr<const std::string>& version,
      KeyType type)
  {
    return KineticStatus(StatusCode::OK, "");
  }

  //! See documentation in superclass.
  kinetic::KineticStatus remove(
      const std::shared_ptr<const std::string>& key,
      KeyType type)
  {
    return KineticStatus(StatusCode::OK, "");
  }

  kinetic::KineticStatus range(
      const std::shared_ptr<const std::string>& start_key,
      const std::shared_ptr<const std::string>& end_key,
      std::unique_ptr<std::vector<std::string>>& keys,
      KeyType type)
  {
    return KineticStatus(StatusCode::OK, "");
  }

  MockCluster()
  {
    _id = "MockCluster";
    _stats.bytes_free = 128;
    _stats.bytes_total = 128;
    _limits.max_key_size = 4096;
    _limits.max_value_size = 128;
    _limits.max_version_size = 4096;
    _version = utility::uuidGenerateEncodeSize(128);
    _value = std::make_shared<const string>('x', 128);
  }

  ~MockCluster()
  { };

private:
  std::shared_ptr<const std::string> _version;
  std::shared_ptr<const std::string> _value;
  kio::ClusterLimits _limits;
  kio::ClusterStats _stats;
  std::string _id;
};

class MockFileIo : public kio::FileIo {
public:

  MockFileIo(std::string path, std::shared_ptr<kio::ClusterInterface> c) : FileIo(path)
  {
    cluster = c;
  }

  ~MockFileIo()
  { };

};

SCENARIO("Cache Performance Test.", "[Cache]")
{
  auto& c = SimulatorController::getInstance();
  c.start(0);
  c.start(1);
  c.start(2);

  GIVEN("A Cache Object and a mocked FileIo object") {

    THEN("go ~") {
      for (int capacity = 1000; capacity < 100000; capacity *= 5) {

        DataCache ccc(capacity * 128);
        std::shared_ptr<ClusterInterface> cluster(new MockCluster());
        MockFileIo fio("kinetic://Cluster1/thepath", cluster);

        printf("Cache get() performance for a cache with capacity of %d items \n", capacity);

        int break_point = capacity * 0.7;
        auto tstart = system_clock::now();
        for (int i = 0; i < break_point; i++) {
          ccc.get((FileIo*) &fio, i, DataBlock::Mode::STANDARD);
        }
        auto tend = system_clock::now();

        printf("%ld items per second up to 70 percent capacity\n",
               (capacity * 700) / (duration_cast<milliseconds>(tend - tstart).count() + 1));

        tstart = system_clock::now();
        for (int i = break_point; i < capacity; i++) {
          ccc.get((FileIo*) &fio, i, DataBlock::Mode::STANDARD);
        }
        tend = system_clock::now();
        printf("%ld items per second up to capacity \n",
               (capacity * 300) / (duration_cast<milliseconds>(tend - tstart).count() + 1));

        tstart = system_clock::now();
        for (int i = capacity; i < 2 * capacity; i++) {
          ccc.get((FileIo*) &fio, i, DataBlock::Mode::STANDARD);
        }
        tend = system_clock::now();
        printf("%ld items per second above capacity \n",
               (capacity * 1000) / (duration_cast<milliseconds>(tend - tstart).count() + 1));

        printf("Waiting for cache items to time out so they qualify for removal\n");
        sleep(6);

        tstart = system_clock::now();
        for (int i = 2 * capacity; i < 3 * capacity; i++) {
          ccc.get((FileIo*) &fio, i, DataBlock::Mode::STANDARD);
        }
        tend = system_clock::now();
        printf("%ld items per second above capacity after timeout \n\n",
               (capacity * 1000) / (duration_cast<milliseconds>(tend - tstart).count() + 1));
      }
    }

  }
}

