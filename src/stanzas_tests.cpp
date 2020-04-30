/*
    Charon - a transport system for GSP data
    Copyright (C) 2019-2020  Autonomous Worlds Ltd

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "private/stanzas.hpp"

#include "testutils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glog/logging.h>

#include <memory>

namespace charon
{
namespace
{

using testing::ElementsAre;
using testing::IsEmpty;

/**
 * Performs a "roundtrip" of serialising and parsing the given StanzaExtension.
 * It is first converted to a gloox Tag, and then the Tag is parsed through
 * newInstance and the result returned as std::unique_ptr.
 *
 * Also performs a clone on the result, just to test this as well.
 */
template <typename T>
  std::unique_ptr<T>
ExtensionRoundtrip (const T& ext)
{
  std::unique_ptr<gloox::Tag> tag(ext.tag ());

  std::unique_ptr<gloox::StanzaExtension> recovered;
  recovered.reset (ext.newInstance (tag.get ()));

  auto* res = dynamic_cast<T*> (recovered->clone ());
  CHECK (res != nullptr);

  return std::unique_ptr<T> (res);
}

/* ************************************************************************** */

using RpcRequestTests = testing::Test;

TEST_F (RpcRequestTests, ParamsArray)
{
  const auto params = ParseJson (R"(["foo", 42])");
  const RpcRequest original("method", params);
  ASSERT_TRUE (original.IsValid ());

  auto recreated = ExtensionRoundtrip (original);
  ASSERT_TRUE (recreated->IsValid ());
  EXPECT_EQ (recreated->GetMethod (), "method");
  EXPECT_EQ (recreated->GetParams (), params);
}

TEST_F (RpcRequestTests, ParamsObject)
{
  const auto params = ParseJson (R"(
    {
      "name": "foo",
      "value": 42
    }
  )");
  const RpcRequest original("method", params);
  ASSERT_TRUE (original.IsValid ());

  auto recreated = ExtensionRoundtrip (original);
  ASSERT_TRUE (recreated->IsValid ());
  EXPECT_EQ (recreated->GetMethod (), "method");
  EXPECT_EQ (recreated->GetParams (), params);
}

/* ************************************************************************** */

using RpcResponseTests = testing::Test;

TEST_F (RpcResponseTests, Success)
{
  const auto result = ParseJson (R"(
    {
      "foo": "bar",
      "count": 42
    }
  )");
  const RpcResponse original(result);
  ASSERT_TRUE (original.IsValid ());
  ASSERT_TRUE (original.IsSuccess ());

  auto recreated = ExtensionRoundtrip (original);
  ASSERT_TRUE (recreated->IsValid ());
  ASSERT_TRUE (recreated->IsSuccess ());
  EXPECT_EQ (recreated->GetResult (), result);
}

TEST_F (RpcResponseTests, ErrorWithData)
{
  const auto data = ParseJson (R"(
    {
      "foo": "bar",
      "count": 42
    }
  )");
  const RpcResponse original(-10, "my error", data);
  ASSERT_TRUE (original.IsValid ());
  ASSERT_FALSE (original.IsSuccess ());

  auto recreated = ExtensionRoundtrip (original);
  ASSERT_TRUE (recreated->IsValid ());
  ASSERT_FALSE (recreated->IsSuccess ());
  EXPECT_EQ (recreated->GetErrorCode (), -10);
  EXPECT_EQ (recreated->GetErrorMessage (), "my error");
  EXPECT_EQ (recreated->GetErrorData (), data);
}

TEST_F (RpcResponseTests, ErrorOnlyCode)
{
  const RpcResponse original(-10, "", Json::Value ());
  ASSERT_TRUE (original.IsValid ());
  ASSERT_FALSE (original.IsSuccess ());

  auto recreated = ExtensionRoundtrip (original);
  ASSERT_TRUE (recreated->IsValid ());
  ASSERT_FALSE (recreated->IsSuccess ());
  EXPECT_EQ (recreated->GetErrorCode (), -10);
  EXPECT_EQ (recreated->GetErrorMessage (), "");
  EXPECT_EQ (recreated->GetErrorData (), Json::Value ());
}

/* ************************************************************************** */

using PongMessageTests = testing::Test;

TEST_F (PongMessageTests, WithoutVersion)
{
  PongMessage original("");
  auto recreated = ExtensionRoundtrip (original);

  ASSERT_TRUE (recreated->IsValid ());
  EXPECT_EQ (recreated->GetVersion (), "");

  std::unique_ptr<gloox::Tag> tag(original.tag ());
  EXPECT_FALSE (tag->hasAttribute ("version"));
}

TEST_F (PongMessageTests, WithVersion)
{
  PongMessage original("version");
  auto recreated = ExtensionRoundtrip (original);

  ASSERT_TRUE (recreated->IsValid ());
  EXPECT_EQ (recreated->GetVersion (), "version");
}

/* ************************************************************************** */

using SupportedNotificationsTests = testing::Test;

TEST_F (SupportedNotificationsTests, NoNotifications)
{
  SupportedNotifications original("pubsub service");
  auto recreated = ExtensionRoundtrip (original);

  ASSERT_TRUE (recreated->IsValid ());
  EXPECT_EQ (recreated->GetService (), "pubsub service");
  EXPECT_THAT (recreated->GetNotifications (), IsEmpty ());
}

TEST_F (SupportedNotificationsTests, WithNotifications)
{
  SupportedNotifications original("pubsub service");
  original.AddNotification ("state", "state node");
  original.AddNotification ("pending", "pending node");
  auto recreated = ExtensionRoundtrip (original);

  ASSERT_TRUE (recreated->IsValid ());
  EXPECT_EQ (recreated->GetService (), "pubsub service");
  EXPECT_THAT (recreated->GetNotifications (), ElementsAre (
    std::make_pair ("pending", "pending node"),
    std::make_pair ("state", "state node")
  ));
}

/* ************************************************************************** */

class NotificationUpdateTests : public testing::Test
{

protected:

  /**
   * Runs a roundtrip test (serialisation and parsing) for the given type
   * and JSON state.
   */
  void
  TestRoundtrip (const std::string& type, const Json::Value& state)
  {
    const NotificationUpdate original(type, state);
    const NotificationUpdate recreated(*original.CreateTag ());

    ASSERT_TRUE (recreated.IsValid ());
    ASSERT_EQ (recreated.GetType (), type);
    ASSERT_EQ (recreated.GetState (), state);
  }

};

TEST_F (NotificationUpdateTests, StringData)
{
  const Json::Value data("JSON <string>");
  TestRoundtrip ("state", data);
}

TEST_F (NotificationUpdateTests, ObjectData)
{
  Json::Value data(Json::objectValue);
  data["foo"] = "bar";
  data["baz"] = 42;

  TestRoundtrip ("pending", data);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
