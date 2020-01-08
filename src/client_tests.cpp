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

#include <atomic>
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
 * Test fixture that has a real Charon client and server.
 */
class ClientTestWithServer : public testing::Test
{

private:

  static constexpr const TestAccount& accServer = ACCOUNTS[0];
  static constexpr const TestAccount& accClient = ACCOUNTS[1];

protected:

  DelayedTestBackend backend;

  Client client;

  ClientTestWithServer ()
    : client(JIDWithoutResource (accServer).bare ())
  {}

  /**
   * Connects the client.
   */
  void
  ConnectClient ()
  {
    client.Connect (JIDWithoutResource (accClient).full (),
                    accClient.password, 0);
  }

  /**
   * Sets up a server connection.
   */
  std::unique_ptr<Server>
  ConnectServer (const std::string& ressource="")
  {
    auto res = std::make_unique<Server> (backend);
    res->Connect (JIDWithResource (accServer, ressource).full (),
                  accServer.password, 0);
    return res;
  }

};

/**
 * Tests basic forwarding of RPC method calls with a Charon client.
 */
class ClientRpcForwardingTests : public ClientTestWithServer
{

protected:

  ClientRpcForwardingTests ()
  {
    ConnectClient ();
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

/**
 * Asynchronous call to WaitForChange in a test.  This is essentially a future
 * whose result we can expect as needed.
 */
class WaitForChangeCall
{

private:

  /** The thread doing the actual call.  */
  std::unique_ptr<std::thread> caller;

  /** Set to true when the thread is done.  */
  std::atomic<bool> done;

  /** The call's result (if any).  */
  Json::Value res;

public:

  /**
   * Constructs a new instance, which will call WaitForChange on the given
   * client with the given arguments.
   */
  explicit WaitForChangeCall (Client& c, const std::string& type,
                              const Json::Value& known)
  {
    done = false;
    caller = std::make_unique<std::thread> ([=, &c] ()
      {
        res = c.WaitForChange (type, known);
        done = true;
      });
  }

  WaitForChangeCall () = delete;
  WaitForChangeCall (const WaitForChangeCall&) = delete;
  void operator= (const WaitForChangeCall&) = delete;

  /**
   * Before the object is destroyed, the result must be awaited / expected.
   */
  ~WaitForChangeCall ()
  {
    CHECK (caller == nullptr) << "Call result not awaited";
  }

  /**
   * Expects that the call is not yet done.
   */
  void
  ExpectRunning ()
  {
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    ASSERT_FALSE (done);
  }

  /**
   * Waits for the result to be in and asserts that it matches the expected
   * JSON value.
   */
  void
  Expect (const Json::Value& expected)
  {
    caller->join ();
    caller.reset ();

    ASSERT_EQ (res, expected);
  }

  /**
   * Waits for the result to be in and asserts that it matches the expected
   * id and value.
   */
  void
  Expect (const std::string& id, const std::string& value)
  {
    Expect (UpdatableState::GetStateJson (id, value));
  }

};

/**
 * Tests notification updates in the client.
 */
class ClientNotificationTests : public ClientTestWithServer
{

protected:

  /**
   * Connects the client and enables notifications with the given types
   * before doing so.
   */
  void
  ConnectClient (const std::vector<std::string>& types)
  {
    for (const auto& t : types)
      client.AddNotification (
          std::make_unique<UpdatableState::Notification> (t));
    ClientTestWithServer::ConnectClient ();
  }

  /**
   * Calls WaitForChange on the client and returns the call handle.
   */
  std::unique_ptr<WaitForChangeCall>
  CallWaitForChange (const std::string& type, const Json::Value& known)
  {
    return std::make_unique<WaitForChangeCall> (client, type, known);
  }

};

TEST_F (ClientNotificationTests, SelectsServerWithNotifications)
{
  ConnectClient ({"foo", "bar"});
  client.SetTimeout (std::chrono::milliseconds (100));

  auto s1 = ConnectServer ("nothing");
  auto s2 = ConnectServer ("service only");
  s2->AddPubSub (PUBSUB_SERVICE);
  auto s3 = ConnectServer ("partial notifications");
  s3->AddPubSub (PUBSUB_SERVICE);

  UpdatableState upd;
  s3->AddNotification (upd.NewWaiter ("foo"));
  s3->AddNotification (upd.NewWaiter ("baz"));

  std::this_thread::sleep_for (std::chrono::milliseconds (100));
  EXPECT_EQ (client.GetServerResource (), "");

  auto s4 = ConnectServer ("good");
  s4->AddPubSub (PUBSUB_SERVICE);
  s4->AddNotification (upd.NewWaiter ("foo"));
  s4->AddNotification (upd.NewWaiter ("bar"));

  std::this_thread::sleep_for (std::chrono::milliseconds (100));
  EXPECT_EQ (client.GetServerResource (), "good");

  /* Disconnect servers before the UpdatableState goes out of scope.  */
  s1.reset ();
  s2.reset ();
  s3.reset ();
  s4.reset ();
}

TEST_F (ClientNotificationTests, WaitForChange)
{
  ConnectClient ({"foo"});

  auto s = ConnectServer ();
  s->AddPubSub (PUBSUB_SERVICE);

  UpdatableState upd;
  s->AddNotification (upd.NewWaiter ("foo"));

  /* Force subscriptions to be finalised by now.  */
  client.GetServerResource ();

  auto w = CallWaitForChange ("foo", "");
  w->ExpectRunning ();

  upd.SetState ("a", "first");
  w->Expect ("a", "first");

  w = CallWaitForChange ("foo", "x");
  w->Expect ("a", "first");

  w = CallWaitForChange ("foo", "a");
  w->ExpectRunning ();

  upd.SetState ("b", "second");
  w->Expect ("b", "second");

  s.reset ();
}

TEST_F (ClientNotificationTests, AlwaysBlock)
{
  ConnectClient ({"foo"});

  auto s = ConnectServer ();
  s->AddPubSub (PUBSUB_SERVICE);

  UpdatableState upd;
  s->AddNotification (upd.NewWaiter ("foo"));

  /* Force subscriptions to be finalised by now.  */
  client.GetServerResource ();

  upd.SetState ("a", "first");

  auto w = CallWaitForChange ("foo", "x");
  w->Expect ("a", "first");

  w = CallWaitForChange ("foo", "always block");
  w->ExpectRunning ();
  upd.SetState ("b", "second");
  w->Expect ("b", "second");

  s.reset ();
}

TEST_F (ClientNotificationTests, TwoNotifications)
{
  ConnectClient ({"foo", "bar"});

  auto s = ConnectServer ();
  s->AddPubSub (PUBSUB_SERVICE);

  UpdatableState upd1;
  s->AddNotification (upd1.NewWaiter ("foo"));
  UpdatableState upd2;
  s->AddNotification (upd2.NewWaiter ("bar"));

  client.GetServerResource ();

  auto w1 = CallWaitForChange ("foo", "");
  auto w2 = CallWaitForChange ("bar", "");

  w1->ExpectRunning ();
  upd1.SetState ("a", "first");
  w1->Expect ("a", "first");

  w2->ExpectRunning ();
  upd2.SetState ("a", "second");
  w2->Expect ("a", "second");

  s.reset ();
}

TEST_F (ClientNotificationTests, TwoServers)
{
  ConnectClient ({"foo"});

  auto s1 = ConnectServer ("srv1");
  s1->AddPubSub (PUBSUB_SERVICE);
  UpdatableState upd1;
  s1->AddNotification (upd1.NewWaiter ("foo"));

  ASSERT_EQ (client.GetServerResource (), "srv1");

  auto s2 = ConnectServer ("srv2");
  s2->AddPubSub (PUBSUB_SERVICE);
  UpdatableState upd2;
  s2->AddNotification (upd1.NewWaiter ("foo"));

  auto w = CallWaitForChange ("foo", "always block");
  upd2.SetState ("a", "wrong");
  w->ExpectRunning ();
  upd1.SetState ("a", "correct");
  w->Expect ("a", "correct");

  s1.reset ();
  s2.reset ();
}

TEST_F (ClientNotificationTests, ServerReselection)
{
  ConnectClient ({"foo"});

  UpdatableState upd;

  auto s = ConnectServer ("srv1");
  s->AddPubSub (PUBSUB_SERVICE);
  s->AddNotification (upd.NewWaiter ("foo"));

  EXPECT_EQ (client.GetServerResource (), "srv1");
  auto w = CallWaitForChange ("foo", "");

  s = ConnectServer ("srv2");
  s->AddPubSub (PUBSUB_SERVICE);
  s->AddNotification (upd.NewWaiter ("foo"));

  /* Note that reselection only happens when an explicit action triggers it.
     We do this by selecting (and checking) the server resource again.  */
  EXPECT_EQ (client.GetServerResource (), "srv2");

  w->ExpectRunning ();
  upd.SetState ("a", "value");
  w->Expect ("a", "value");

  /* Try again.  This time we trigger reselection through the call itself,
     and only use GetServerResource to force all subscriptions to be done
     before updating the server state.  */
  s = ConnectServer ("srv3");
  s->AddPubSub (PUBSUB_SERVICE);
  s->AddNotification (upd.NewWaiter ("foo"));

  w = CallWaitForChange ("foo", "always block");
  EXPECT_EQ (client.GetServerResource (), "srv3");
  w->ExpectRunning ();
  upd.SetState ("b", "value 2");
  w->Expect ("b", "value 2");

  s.reset ();
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
