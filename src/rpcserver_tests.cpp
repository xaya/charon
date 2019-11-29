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

#include "rpcserver.hpp"

#include "rpc-stubs/testbackendserverstub.h"
#include "testutils.hpp"

#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gtest/gtest.h>

#include <glog/logging.h>

namespace charon
{
namespace
{

/** RPC port for our test backend server.  */
constexpr int RPC_PORT = 42042;

/** HTTP URL for the test backend server.  */
constexpr const char* RPC_URL = "http://localhost:42042";

/* ************************************************************************** */

/**
 * Implementation of the "TestBackend" RPC server.
 */
class TestBackendServer
{

private:

  class Implementation : public TestBackendServerStub
  {

  public:

    explicit Implementation (jsonrpc::AbstractServerConnector& conn)
      : TestBackendServerStub(conn)
    {}

    int
    echobypos (const int val) override
    {
      return val;
    }

    int
    echobyname (const int val) override
    {
      return val;
    }

    int
    error (const int code, const Json::Value& data,
           const std::string& msg) override
    {
      throw jsonrpc::JsonRpcException (code, msg, data);
    }

    int
    donotcall () override
    {
      LOG (FATAL) << "backend method donotcall was called";
    }

  };

  /** The underlying HTTP server connector.  */
  jsonrpc::HttpServer http;

  /** The server implementation.  */
  Implementation server;

public:

  TestBackendServer ()
    : http(RPC_PORT), server(http)
  {
    server.StartListening ();
  }

  ~TestBackendServer ()
  {
    server.StopListening ();
  }

};

/* ************************************************************************** */

class ForwardingRpcServerTests : public testing::Test
{

private:

  TestBackendServer backend;

protected:

  ForwardingRpcServer server;

  ForwardingRpcServerTests ()
    : server(RPC_URL)
  {
    server.AllowMethod ("echobypos");
    server.AllowMethod ("echobyname");
    server.AllowMethod ("error");
  }

};

TEST_F (ForwardingRpcServerTests, PositionalArguments)
{
  EXPECT_EQ (server.HandleMethod ("echobypos", ParseJson ("[5]")), 5);
}

TEST_F (ForwardingRpcServerTests, KeywordArguments)
{
  EXPECT_EQ (server.HandleMethod ("echobyname", ParseJson (R"({"value": 10})")),
             10);
}

TEST_F (ForwardingRpcServerTests, Error)
{
  try
    {
      server.HandleMethod ("error", ParseJson (R"(
        {
          "code": 42,
          "msg": "error",
          "data":
            {
              "foo": "bar"
            }
        }
      )"));
      FAIL () << "Expected error not thrown";
    }
  catch (const RpcServer::Error& exc)
    {
      LOG (INFO) << "Caught expected error: " << exc.what ();
      EXPECT_EQ (exc.GetCode (), 42);
      EXPECT_EQ (exc.GetMessage (), "error");
      ASSERT_TRUE (exc.GetData ().isObject ());
      EXPECT_EQ (exc.GetData ()["foo"], "bar");
    }
}

TEST_F (ForwardingRpcServerTests, MethodNotAllowed)
{
  try
    {
      server.HandleMethod ("donotcall", ParseJson ("[]"));
      FAIL () << "Expected error not thrown";
    }
  catch (const RpcServer::Error& exc)
    {
      LOG (INFO) << "Caught expected error: " << exc.what ();
      EXPECT_EQ (exc.GetCode (), jsonrpc::Errors::ERROR_RPC_METHOD_NOT_FOUND);
    }
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
