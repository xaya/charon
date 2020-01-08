/*
    Charon - a transport system for GSP data
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#include "rpcwaiter.hpp"

#include "testutils.hpp"

#include <jsonrpccpp/server.h>
#include <jsonrpccpp/server/connectors/httpserver.h>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <thread>

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
 * Implementation of the RPC backend server we use for testing.
 */
class TestBackendServer
{

private:

  class Implementation : public jsonrpc::AbstractServer<Implementation>
  {

  public:

    explicit Implementation (jsonrpc::AbstractServerConnector& conn)
      : jsonrpc::AbstractServer<Implementation>(conn,
                                                jsonrpc::JSONRPC_SERVER_V2)
    {
      const jsonrpc::Procedure proc("wait", jsonrpc::PARAMS_BY_POSITION,
                                    jsonrpc::JSON_STRING, nullptr);
      bindAndAddMethod (proc, &Implementation::wait);
    }

    void
    wait (const Json::Value& params, Json::Value& res)
    {
      ASSERT_EQ (params, ParseJson (R"(["always block"])"));

      /* The sleep here must be longer than the "short" HTTP client timeout
         set on the RpcUpdateWaiter.  */
      std::this_thread::sleep_for (std::chrono::milliseconds (100));

      res = "new state";
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

} // anonymous namespace

class RpcUpdateWaiterTests : public testing::Test
{

private:

  TestBackendServer backend;

protected:

  RpcUpdateWaiter waiter;

  RpcUpdateWaiterTests ()
    : waiter(RPC_URL, "wait", "always block")
  {}

  void
  EnableShortTimeout ()
  {
    waiter.ShortTimeout ();
  }

};

namespace
{

TEST_F (RpcUpdateWaiterTests, CallsWork)
{
  Json::Value newState;
  ASSERT_TRUE (waiter.WaitForUpdate (newState));
  EXPECT_EQ (newState, "new state");
}

TEST_F (RpcUpdateWaiterTests, ClientTimeout)
{
  EnableShortTimeout ();

  Json::Value newState;
  EXPECT_FALSE (waiter.WaitForUpdate (newState));
}

TEST_F (RpcUpdateWaiterTests, ConcurrentCalls)
{
  EXPECT_DEATH (
    {
      std::thread asyncCall([this] ()
        {
          Json::Value newState;
          waiter.WaitForUpdate (newState);
        });

      Json::Value newState;
      waiter.WaitForUpdate (newState);
    }, "Concurrent calls");
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
