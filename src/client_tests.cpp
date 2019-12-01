// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "client.hpp"

#include "server.hpp"
#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"
#include "testutils.hpp"

#include <gloox/message.h>
#include <gloox/messagehandler.h>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <thread>
#include <vector>

namespace charon
{
namespace
{

/* ************************************************************************** */

/**
 * Test case for the client's server discovery functionality.  It runs a custom
 * XMPP client that answers pings after a specific delay rather than
 * immediately.
 */
class ClientServerDiscoveryTests : public testing::Test,
                                   private XmppClient,
                                   private gloox::MessageHandler
{

protected:

  /** The delay for sending the pong reply.  */
  static constexpr auto PONG_DELAY = std::chrono::milliseconds (100);

private:

  void
  handleMessage (const gloox::Message& msg,
                 gloox::MessageSession* session) override
  {
    const auto* ext = msg.findExtension<PingMessage> (PingMessage::EXT_TYPE);
    if (ext != nullptr)
      {
        LOG (INFO) << "Sleeping before answering ping...";
        std::this_thread::sleep_for (PONG_DELAY);

        LOG (INFO) << "Sleep done, sending pong now";
        gloox::Message reply(gloox::Message::Normal, msg.from ());
        reply.addExtension (new PongMessage ());

        RunWithClient ([&reply] (gloox::Client& c)
          {
            c.send (reply);
          });
      }
  }

protected:

  static constexpr const TestAccount& accServer = ACCOUNTS[0];
  static constexpr const TestAccount& accClient = ACCOUNTS[1];
  static constexpr const char* SERVER_RES = "test";

  Client client;

  ClientServerDiscoveryTests ()
    : XmppClient(JIDWithResource (accServer, SERVER_RES), accServer.password),
      client(JIDWithoutResource (accServer).bare ())
  {
    RunWithClient ([this] (gloox::Client& c)
      {
        c.registerStanzaExtension (new RpcRequest ());
        c.registerStanzaExtension (new RpcResponse ());
        c.registerStanzaExtension (new PingMessage ());
        c.registerStanzaExtension (new PongMessage ());

        c.registerMessageHandler (this);
      });

    client.Connect (JIDWithoutResource (accClient).full (),
                    accClient.password, 0);
    Connect (0);
  }

};

constexpr const char* ClientServerDiscoveryTests::SERVER_RES;

TEST_F (ClientServerDiscoveryTests, FindsServerResource)
{
  client.SetTimeout (2 * PONG_DELAY);
  EXPECT_EQ (client.GetServerResource (), SERVER_RES);
}

TEST_F (ClientServerDiscoveryTests, Timeout)
{
  client.SetTimeout (PONG_DELAY / 2);
  EXPECT_EQ (client.GetServerResource (), "");
}

TEST_F (ClientServerDiscoveryTests, MultipleThreads)
{
  client.SetTimeout (2 * PONG_DELAY);

  std::vector<std::thread> threads;
  for (unsigned i = 0; i < 5; ++i)
    threads.emplace_back ([this, i] ()
      {
        LOG (INFO) << "Requesting from thread " << i << "...";
        EXPECT_EQ (client.GetServerResource (), SERVER_RES);
      });

  for (auto& t : threads)
    t.join ();
}

/* ************************************************************************** */

/**
 * RpcServer that uses TestBackend, but applies a configurable delay on top
 * of it (i.e. delays responding to methods).
 */
class DelayedTestBackend : public TestBackend
{

private:

  /** The delay for each method call.  */
  std::chrono::milliseconds delay;

public:

  DelayedTestBackend ()
    : delay(0)
  {}

  template <typename Rep, typename Period>
    void
    SetDelay (const std::chrono::duration<Rep, Period>& d)
  {
    delay = std::chrono::duration_cast<decltype (delay)> (d);
  }

  Json::Value
  HandleMethod (const std::string& method, const Json::Value& params) override
  {
    std::this_thread::sleep_for (delay);
    return TestBackend::HandleMethod (method, params);
  }

};

/**
 * Tests basic forwarding of RPC method calls with a Charon client.
 */
class ClientRpcForwardingTests : public testing::Test
{

private:

  static constexpr const TestAccount& accServer = ACCOUNTS[0];
  static constexpr const TestAccount& accClient = ACCOUNTS[1];

protected:

  DelayedTestBackend backend;

  Client client;

  ClientRpcForwardingTests ()
    : client(JIDWithoutResource (accServer).bare ())
  {
    client.Connect (JIDWithoutResource (accClient).full (),
                    accClient.password, 0);
  }

  /**
   * Sets up a server connection.
   */
  std::unique_ptr<Server>
  ConnectServer ()
  {
    auto res = std::make_unique<Server> (backend);
    res->Connect (JIDWithoutResource (accServer).bare (),
                  accServer.password, 0);
    return res;
  }

};

TEST_F (ClientRpcForwardingTests, CallSuccess)
{
  auto srv = ConnectServer ();
  EXPECT_EQ (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")), "foo");
}

TEST_F (ClientRpcForwardingTests, CallError)
{
  auto srv = ConnectServer ();
  EXPECT_THROW (client.ForwardMethod ("error", ParseJson (R"(["foo"])")),
                RpcServer::Error);
}

TEST_F (ClientRpcForwardingTests, NoServer)
{
  client.SetTimeout (std::chrono::milliseconds (100));
  EXPECT_THROW (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")),
                RpcServer::Error);
}

TEST_F (ClientRpcForwardingTests, CallTimeout)
{
  auto srv = ConnectServer ();
  client.SetTimeout (std::chrono::milliseconds (10));
  backend.SetDelay (std::chrono::milliseconds (100));
  EXPECT_THROW (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")),
                RpcServer::Error);
}

TEST_F (ClientRpcForwardingTests, MultipleThreads)
{
  auto srv = ConnectServer ();

  client.SetTimeout (std::chrono::milliseconds (500));
  backend.SetDelay (std::chrono::milliseconds (10));

  std::vector<std::thread> threads;
  for (unsigned i = 0; i < 5; ++i)
    threads.emplace_back ([this, i] ()
      {
        LOG (INFO) << "Calling from thread " << i << "...";
        EXPECT_EQ (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")),
                   "foo");
      });

  for (auto& t : threads)
    t.join ();
}

/* FIXME: Also test what happens if the selected server gets disconnected,
   i.e. we get an XMPP "service unavailable" error.  In that case, we should
   try to reselect the XMPP server JID.  */

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
