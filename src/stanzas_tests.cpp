/*
    Charon - a transport system for GSP data
    Copyright (C) 2019  Autonomous Worlds Ltd

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

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <memory>

namespace charon
{
namespace
{

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
  Json::Value params(Json::arrayValue);
  params.append ("foo");
  params.append (42);

  const RpcRequest original("method", params);
  ASSERT_TRUE (original.IsValid ());

  auto recreated = ExtensionRoundtrip (original);
  ASSERT_TRUE (recreated->IsValid ());
  EXPECT_EQ (recreated->GetMethod (), "method");
  EXPECT_EQ (recreated->GetParams (), params);
}

TEST_F (RpcRequestTests, ParamsObject)
{
  Json::Value params(Json::objectValue);
  params["name"] = "foo";
  params["count"] = 42;

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
  Json::Value result(Json::objectValue);
  result["foo"] = "bar";
  result["count"] = 42;

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
  Json::Value data(Json::objectValue);
  data["foo"] = "bar";
  data["count"] = 42;

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

} // anonymous namespace
} // namespace charon
