// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "server.hpp"

#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"
#include "rpcserver.hpp"
#include "testutils.hpp"

#include <gloox/iq.h>
#include <gloox/iqhandler.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <glog/logging.h>

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>

namespace charon
{
namespace
{

using testing::IsEmpty;

/* ************************************************************************** */

/**
 * Backend for answering RPC calls in a dummy fashion.
 */
class Backend : public RpcServer
{

public:

  Backend () = default;

  Json::Value
  HandleMethod (const std::string& method, const Json::Value& params) override
  {
    CHECK (params.isArray ());
    CHECK_EQ (params.size (), 1);
    CHECK (params[0].isString ());

    if (method == "echo")
      return params[0];

    if (method == "error")
      throw Error (42, params[0].asString (), Json::Value ());

    LOG (FATAL) << "Unexpected method: " << method;
  }

};

/**
 * A list of received IQ results.  This is what we use to handle the
 * incoming IQs, and what we use for verifying them later on.
 *
 * For simplicity in the tests, we expect all results to be strings.  If we
 * receive a JSON-RPC error instead, we just set the string to "error <msg>".
 */
class ReceivedIqResults : public gloox::IqHandler
{

private:

  /** The results themselves, keyed by "context".  */
  std::map<int, std::string> results;

  /**
   * Mutex for synchronising the receiving messages and waiting for them
   * when comparing to expectations.
   */
  std::mutex mut;

  /** Condition variable for waiting for more messages.  */
  std::condition_variable cv;

public:

  ReceivedIqResults () = default;

  ~ReceivedIqResults ()
  {
    EXPECT_THAT (results, IsEmpty ()) << "Unexpected IQ results received";
  }

  bool
  handleIq (const gloox::IQ& iq) override
  {
    LOG (FATAL) << "Received IQ without context";
  }

  void
  handleIqID (const gloox::IQ& iq, const int context) override
  {
    LOG (INFO) << "Received IQ response for context " << context;
    ASSERT_EQ (iq.subtype (), gloox::IQ::Result);

    const auto* ext = iq.findExtension<RpcResponse> (RpcResponse::EXT_TYPE);
    CHECK (ext != nullptr) << "No expected RpcResult extension";

    std::string result;
    if (ext->IsSuccess ())
      result = ext->GetResult ().asString ();
    else
      result = "error " + ext->GetErrorMessage ();

    std::lock_guard<std::mutex> lock(mut);
    const auto ins = results.emplace (context, result);
    ASSERT_TRUE (ins.second) << "Duplicate context: " << context;

    cv.notify_all ();
  }

  /**
   * Expects to receive the given messages.  Waits for them to
   * arrive as needed, and clears out the message queue at the end.
   */
  void
  Expect (const std::map<int, std::string>& expected)
  {
    std::unique_lock<std::mutex> lock(mut);
    while (results.size () < expected.size ())
      {
        LOG (INFO) << "Waiting for more messages to be received...";
        cv.wait (lock);
      }

    EXPECT_EQ (results, expected);
    results.clear ();
  }

};

/**
 * Test case that runs a Charon server as well as a custom XMPP client for
 * sending IQ requests to the server.
 */
class ServerTests : public testing::Test, private XmppClient
{

private:

  static constexpr const TestAccount& accServer = ACCOUNTS[0];
  static constexpr const TestAccount& accClient = ACCOUNTS[1];
  static constexpr const char* SERVER_RES = "test";

  Backend backend;

protected:

  ReceivedIqResults results;
  Server server;

  ServerTests ()
    : XmppClient(JIDWithoutResource (accClient), accClient.password),
      server(backend)
  {
    RunWithClient ([] (gloox::Client& c)
      {
        c.registerStanzaExtension (new RpcRequest ());
        c.registerStanzaExtension (new RpcResponse ());
      });

    server.Connect (JIDWithResource (accServer, SERVER_RES).full (),
                    accServer.password, 0);
    Connect (0);
  }

  /**
   * Sends a new request to the server.
   */
  void
  SendMessage (const int context, const std::string& method,
               const std::string& param)
  {
    LOG (INFO)
        << "Sending request for context " << context << ": "
        << method << " " << param;

    const gloox::JID jidTo = JIDWithResource (accServer, SERVER_RES);
    gloox::IQ iq(gloox::IQ::Get, jidTo);

    Json::Value params(Json::arrayValue);
    params.append (param);
    auto req = std::make_unique<RpcRequest> (method, params);
    iq.addExtension (req.release ());

    RunWithClient ([this, context, &iq] (gloox::Client& c)
      {
        c.send (iq, &results, context);
      });
  }

};

/* ************************************************************************** */

TEST_F (ServerTests, Success)
{
  SendMessage (42, "echo", "foo");
  results.Expect ({{42, "foo"}});
}

TEST_F (ServerTests, Error)
{
  SendMessage (42, "error", "foo");
  results.Expect ({{42, "error foo"}});
}

TEST_F (ServerTests, MultipleRequests)
{
  SendMessage (1, "echo", "foo");
  SendMessage (2, "error", "bar");
  SendMessage (3, "echo", "baz");
  results.Expect (
    {
      {1, "foo"},
      {2, "error bar"},
      {3, "baz"},
    }
  );
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
