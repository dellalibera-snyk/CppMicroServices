/*=============================================================================

  Library: CppMicroServices

  Copyright (c) The CppMicroServices developers. See the COPYRIGHT
  file at the top-level directory of this distribution and at
  https://github.com/CppMicroServices/CppMicroServices/COPYRIGHT .

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  =============================================================================*/
#if NEVER
#    include <algorithm>
#    include <chrono>
#    include <cstdio>
#    include <iostream>

// #include "version.h"

#    include "gtest/gtest.h"

#    include "cppmicroservices/servicecomponent/detail/ComponentInstanceImpl.hpp"
#    include <cppmicroservices/ServiceInterface.h>

using cppmicroservices::service::component::detail::ComponentInstanceImpl;

namespace
{

    // dummy types used for testing
    struct TestServiceInterface1
    {
    };
    struct TestServiceInterface2
    {
    };

    class TestServiceImpl3 : public TestServiceInterface1
    {
    };

    /**
     * This test point is used to verify a compile error is generated when a service component
     * description specifies an interface that is not implemented by the implementation class
     *
     * @todo Automate this test point. Currently interactive is the only way to verify compilation failures.
     */
    TEST(ComponentInstance, ValidateInheritance)
    {
        ComponentInstanceImpl<TestServiceImpl3, std::tuple<TestServiceInterface2>> compInstance; // compile error
    }
} // namespace

int
main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#endif
