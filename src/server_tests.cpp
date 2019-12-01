// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "server.hpp"

#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"
#include "testutils.hpp"

#include <gloox/iq.h>
#include <gloox/iqhandler.h>
#include <gloox/message.h>
#include <gloox/messagehandler.h>

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
class ServerTests : public testing::Test, protected XmppClient
{

private:

  TestBackend backend;

protected:

  static constexpr const TestAccount& accServer = ACCOUNTS[0];
  static constexpr const TestAccount& accClient = ACCOUNTS[1];
  static constexpr const char* SERVER_RES = "test";

  Server server;

  ServerTests ()
    : XmppClient(JIDWithoutResource (accClient), accClient.password),
      server(backend)
  {
    RunWithClient ([] (gloox::Client& c)
      {
        c.registerStanzaExtension (new RpcRequest ());
        c.registerStanzaExtension (new RpcResponse ());
        c.registerStanzaExtension (new PingMessage ());
        c.registerStanzaExtension (new PongMessage ());
      });

    server.Connect (JIDWithResource (accServer, SERVER_RES).full (),
                    accServer.password, 0);
    Connect (0);
  }

};

constexpr const char* ServerTests::SERVER_RES;

/* ************************************************************************** */

/**
 * Test case for the initial ping/pong recovery of the server full JID.
 */
class ServerPingTests : public ServerTests, private gloox::MessageHandler
{

private:

  /** Signals when a pong message has been handled.  */
  std::condition_variable cv;

  /** Mutex for cv.  */
  std::mutex mut;

  /** Resource of the sender of pong.  */
  std::string pongResource;

  void
  handleMessage (const gloox::Message& msg,
                 gloox::MessageSession* session) override
  {
    VLOG (1) << "Processing message from " << msg.from ().full ();

    auto* ext = msg.findExtension (PongMessage::EXT_TYPE);
    if (ext != nullptr)
      {
        LOG (INFO) << "Received pong from " << msg.from ().full ();

        std::lock_guard<std::mutex> lock(mut);
        pongResource = msg.from ().resource ();
        cv.notify_all ();
      }
  }

protected:

  ServerPingTests ()
  {
    RunWithClient ([this] (gloox::Client& c)
      {
        c.registerMessageHandler (this);
      });
  }

  /**
   * Sends a ping to the given JID.
   */
  void
  SendPing (const gloox::JID& to)
  {
    gloox::Message msg(gloox::Message::Normal, to);
    msg.addExtension (new PingMessage ());

    RunWithClient ([&msg] (gloox::Client& c)
      {
        c.send (msg);
      });
  }

  /**
   * Waits for receipt of a pong message.  Returns the pong sender's resource.
   */
  std::string
  WaitForPong ()
  {
    std::unique_lock<std::mutex> lock(mut);
    while (pongResource.empty ())
      cv.wait (lock);

    return pongResource;
  }

};

TEST_F (ServerPingTests, GetResource)
{
  SendPing (JIDWithoutResource (accServer));
  EXPECT_EQ (WaitForPong (), SERVER_RES);
}

/* ************************************************************************** */

/**
 * Test case for answering ordinary RPC requests sent as IQs to the server.
 */
class ServerRpcTests : public ServerTests
{

protected:

  ReceivedIqResults results;

  /**
   * Sends a new request to the server.
   */
  void
  SendRequest (const int context, const std::string& method,
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

TEST_F (ServerRpcTests, Success)
{
  SendRequest (42, "echo", "foo");
  results.Expect ({{42, "foo"}});
}

TEST_F (ServerRpcTests, Error)
{
  SendRequest (42, "error", "foo");
  results.Expect ({{42, "error foo"}});
}

TEST_F (ServerRpcTests, MultipleRequests)
{
  SendRequest (1, "echo", "foo");
  SendRequest (2, "error", "bar");
  SendRequest (3, "echo", "baz");
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
