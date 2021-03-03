/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*           Copyright (C)2014-2021, WWIV Software Services               */
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

#include "bbs/bbs.h"
#include "bbs_test/bbs_helper.h"
#include <iostream>
#include <string>

using namespace wwiv::common;
using std::cout;
using std::endl;
using std::string;


class BbsMacroContextTest : public ::testing::Test {
protected:
  void SetUp() override {
    helper.SetUp();
    PipeEval pipe_eval(a()->context());
    bc = std::make_unique<BbsMacroContext>(&a()->context(), pipe_eval);
  }

  BbsHelper helper{};
  std::unique_ptr<BbsMacroContext> bc;
};

TEST_F(BbsMacroContextTest, Move_Up2) {
  std::string s1 = "[2A";
  auto it = std::cbegin(s1);

  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::movement);
  EXPECT_EQ(i.up, 2);
}

TEST_F(BbsMacroContextTest, Move_Up) {
  std::string s1 = "[A";
  auto it = std::cbegin(s1);

  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::movement);
  EXPECT_EQ(i.up, 1);
}

TEST_F(BbsMacroContextTest, Move_Down) {
  std::string s1 = "[2B";
  auto it = std::cbegin(s1);
  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::movement);
  EXPECT_EQ(i.up, 0);
  EXPECT_EQ(i.down, 2);
}

TEST_F(BbsMacroContextTest, Move_Left) {
  std::string s1 = "[2D";
  auto it = std::cbegin(s1);

  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::movement);
  EXPECT_EQ(i.left, 2);
}

TEST_F(BbsMacroContextTest, Move_Right) {
  std::string s1 = "[2C";
  auto it = std::cbegin(s1);

  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::movement);
  EXPECT_EQ(i.right, 2);
}

TEST_F(BbsMacroContextTest, Move_XY) {
  std::string s1 = "[10;20H";
  auto it = std::cbegin(s1);

  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::movement);
  EXPECT_EQ(i.x, 9);
  EXPECT_EQ(i.y, 19);
}

TEST_F(BbsMacroContextTest, Expr_Ending) {
  std::string s1 = "{hello}";
  auto it = std::cbegin(s1);
  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::text);
  EXPECT_EQ(i.text, "{hello}");
}

TEST_F(BbsMacroContextTest, Expr_WithAdditional) {
  std::string s1 = "{hello}Hello";
  auto it = std::cbegin(s1);
  const auto i = bc->interpret(it, std::end(s1));
  EXPECT_EQ(i.cmd, interpreted_cmd_t::text);
  EXPECT_EQ(i.text, "{hello}");
  EXPECT_EQ("Hello", std::string(it, std::cend(s1)));
}

TEST_F(BbsMacroContextTest, JustText) {
  std::string s1 = "hello";
  auto it = std::cbegin(s1);
  const auto end = std::cend(s1);
  const auto i = bc->interpret(it, end);
  EXPECT_EQ(i.cmd, interpreted_cmd_t::text);
  EXPECT_EQ(i.text, "h");
  EXPECT_EQ('e', *it);
  EXPECT_EQ(it, std::cbegin(s1) + 1);
}
