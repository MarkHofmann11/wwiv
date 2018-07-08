/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*               Copyright (C)2014-2017, WWIV Software Services           */
/*                                                                        */
/*    Licensed  under the  Apache License, Version  2.0 (the "License");  */
/*    you may not use this  file  except in compliance with the License.  */
/*    You may obtain a copy of the License at                             */
/*                                                                        */
/*                http://www.apache.org/licenses/LICENSE-2.0              */
/*                                                                        */
/*    Unless  required  by  applicable  law  or agreed to  in  writing,   */
/*    software  distributed  under  the  License  is  distributed on an   */
/*    "AS IS"  BASIS, WITHOUT  WARRANTIES  OR  CONDITIONS OF ANY  KIND,   */
/*    either  express  or implied.  See  the  License for  the specific   */
/*    language governing permissions and limitations under the License.   */
/*                                                                        */
/**************************************************************************/
#include "gtest/gtest.h"
#include "core/fake_clock.h"

#include <string>

using std::map;
using std::string;
using std::vector;

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace wwiv::core;

TEST(FakeClock, Simple) { 
  FakeClock c{DateTime::now()}; 
  auto start = c.Now();
  c.tick(2s);
  auto mid = c.Now();
  c.tick(3s);
  auto end = c.Now();

  auto duration = duration_cast<seconds>(mid.to_system_clock() - start.to_system_clock());
  EXPECT_EQ(2s, duration);
}
