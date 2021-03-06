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

#define CATCH_CONFIG_MAIN

#include <kio/KineticIoFactory.hh>
#include <dlfcn.h>
#include <system_error>
#include "catch.hpp"

SCENARIO("Kineticio Dynamic Loading Test", "[Dynload]") {

  /* Set environment variables so that KineticIo can find the test json configuration.
  * TESTJSON_LOCATION is set by cmake. */
  std::string location(getenv("KINETIC_DRIVE_LOCATION") ? getenv("KINETIC_DRIVE_LOCATION") : "" );
  setenv("KINETIC_DRIVE_LOCATION", TESTJSON_LOCATION, 1);
  std::string security(getenv("KINETIC_DRIVE_SECURITY") ? getenv("KINETIC_DRIVE_SECURITY") : "" );
  setenv("KINETIC_DRIVE_SECURITY", TESTJSON_LOCATION, 1);
  std::string cluster(getenv("KINETIC_CLUSTER_DEFINITION") ? getenv("KINETIC_CLUSTER_DEFINITION") : "" );
  setenv("KINETIC_CLUSTER_DEFINITION", TESTJSON_LOCATION, 1);

  GIVEN("Kineticio factory creation and destruction methods can be loaded from library.") {
#ifdef __APPLE__
    void* handle = dlopen("./libkineticio.dylib", RTLD_NOW);
#else
    void* handle = dlopen("./libkineticio.so", RTLD_NOW);
#endif
    REQUIRE(handle);

    typedef kio::LoadableKineticIoFactoryInterface* (* function_t)();
    function_t get_factory = (function_t) dlsym(handle, "getKineticIoFactory");
    REQUIRE_FALSE(dlerror());

    THEN("A factory object can be acessed.") {
      auto ioFactory = get_factory();
      REQUIRE(ioFactory);

      AND_THEN("We can run some of the basic sanity tests on a fileio object constructed by the factory") {

        GIVEN("Wrong urls") {
          THEN("fileio object creation throws with EINVAL on illegally constructed urls") {
            std::string path("path");
            REQUIRE_THROWS_AS(ioFactory->makeFileIo(path), std::system_error);
            try {
              ioFactory->makeFileIo(path);
            } catch (const std::system_error& e) {
              REQUIRE((e.code().value() == EINVAL));
            }
          }
          THEN("fileio object creation throws with ENODEV on nonexisting clusters") {
            std::string path("kinetic://thisdoesntexist/file");
            REQUIRE_THROWS_AS(ioFactory->makeFileIo(path), std::system_error);
            try {
              ioFactory->makeFileIo(path);
            } catch (const std::system_error& e) {
              REQUIRE((e.code().value() == ENODEV));
            }
          }
        }
      }
    }
    dlclose(handle);
  }

  /* Reset environment variables back to the initial values. */
  setenv("KINETIC_DRIVE_LOCATION", location.c_str(), 1);
  setenv("KINETIC_DRIVE_SECURITY", security.c_str(), 1);
  setenv("KINETIC_CLUSTER_DEFINITION", security.c_str(), 1);
}