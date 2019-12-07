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
#include <gloox/presence.h>
#include <gloox/presencehandler.h>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <condition_variable>
#include <mutex>
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
                                   private gloox::MessageHandler,
                                   private gloox::PresenceHandler
{

protected:

  /** The delay for sending the pong reply.  */
  static constexpr auto PONG_DELAY = std::chrono::milliseconds (100);

private:

  /** Condition variable for waiting for the client's directed presence.  */
  std::condition_variable cv;

  /** Mutex for cv.  */
  std::mutex mut;

  /** Set to true when we receive presence from the client.  */
  bool seenClientPresence = false;

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
        gloox::Presence reply(gloox::Presence::Available, msg.from ());
        reply.addExtension (new PongMessage ());

        RunWithClient ([&reply] (gloox::Client& c)
          {
            c.send (reply);
          });
      }
  }

  void
  handlePresence (const gloox::Presence& p) override
  {
    if (p.subtype () != gloox::Presence::Available)
      {
        LOG (WARNING) << "Ignoring non-available presence";
        return;
      }

    if (p.from ().bareJID () != JIDWithoutResource (accClient)
          || p.from ().resource ().empty ())
      {
        LOG (WARNING)
            << "Ignoring presence not from our full client JID, but from "
            << p.from ().full ();
        return;
      }

    LOG (INFO)
        << "Received directed presence from client " << p.from ().full ();

    std::lock_guard<std::mutex> lock(mut);
    seenClientPresence = true;
    cv.notify_all ();
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
        c.registerPresenceHandler (this);
      });

    client.Connect (JIDWithoutResource (accClient).full (),
                    accClient.password, 0);
    Connect (0);
  }

  /**
   * Wait for retrieving the client's presence (final part of the handshake).
   */
  void
  ExpectClientPresence ()
  {
    std::unique_lock<std::mutex> lock(mut);
    while (!seenClientPresence)
      cv.wait (lock);
  }

};

constexpr const char* ClientServerDiscoveryTests::SERVER_RES;

TEST_F (ClientServerDiscoveryTests, FindsServerResource)
{
  client.SetTimeout (2 * PONG_DELAY);
  EXPECT_EQ (client.GetServerResource (), SERVER_RES);
  ExpectClientPresence ();
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

  ExpectClientPresence ();
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

TEST_F (ClientRpcForwardingTests, ServerReselection)
{
  /* Start by connecting a server instance and making a call to it, which
     will also select it in the client.  */
  auto srv = ConnectServer ();
  EXPECT_EQ (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")), "foo");

  /* Disconnect the server and make sure it is "settled" with the XMPP
     server before continuing.  */
  srv.reset ();
  std::this_thread::sleep_for (std::chrono::milliseconds (500));

  /* Reconnect another Charon server instance.  If we then make
     another call, the client should notice the original server is gone
     and should reselect another instance.  */
  srv = ConnectServer ();
  EXPECT_EQ (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")), "foo");
}

TEST_F (ClientRpcForwardingTests, ServerReselectionNotAvailable)
{
  auto srv = ConnectServer ();
  EXPECT_EQ (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")), "foo");

  srv.reset ();
  std::this_thread::sleep_for (std::chrono::milliseconds (500));

  /* No other server instance is available, so the call should time out.  */
  client.SetTimeout (std::chrono::milliseconds (100));
  EXPECT_THROW (client.ForwardMethod ("echo", ParseJson (R"(["foo"])")),
                RpcServer::Error);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
