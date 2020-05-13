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

#include "server.hpp"

#include "private/pubsub.hpp"
#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"
#include "testutils.hpp"

#include <gloox/iq.h>
#include <gloox/iqhandler.h>
#include <gloox/message.h>
#include <gloox/presence.h>
#include <gloox/presencehandler.h>

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

using testing::ElementsAre;
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

} // anonymous namespace

/**
 * Test case that runs a Charon server as well as a custom XMPP client for
 * sending IQ requests to the server.
 */
class ServerTests : public testing::Test, protected XmppClient
{

private:

  TestBackend backend;

protected:

  static constexpr int accServer = 0;
  static constexpr int accClient = 1;

  static constexpr const char* SERVER_RES = "test";
  static constexpr const char* SERVER_VERSION = "version";

  Server server;

  ServerTests ()
    : XmppClient(JIDWithoutResource (GetTestAccount (accClient)),
                 GetTestAccount (accClient).password),
      server(SERVER_VERSION, backend,
             JIDWithResource (GetTestAccount (accServer), SERVER_RES).full (),
             GetTestAccount (accServer).password)
  {
    RunWithClient ([] (gloox::Client& c)
      {
        c.registerStanzaExtension (new RpcRequest ());
        c.registerStanzaExtension (new RpcResponse ());
        c.registerStanzaExtension (new PingMessage ());
        c.registerStanzaExtension (new PongMessage ());
        c.registerStanzaExtension (new SupportedNotifications ());
      });

    server.Connect (0);
    Connect (0);
  }

  /**
   * Returns the pubsub node corresponding to the given notification
   * type in our server.
   */
  const std::string&
  GetNotificationNode (const std::string& type) const
  {
    return server.GetNotificationNode (type);
  }

};

constexpr const char* ServerTests::SERVER_RES;
constexpr const char* ServerTests::SERVER_VERSION;

namespace
{

/* ************************************************************************** */

/**
 * Test case for the initial ping/pong recovery of the server full JID.
 */
class ServerPingTests : public ServerTests, private gloox::PresenceHandler
{

private:

  /** Signals when a pong message has been handled.  */
  std::condition_variable cv;

  /** Mutex for cv.  */
  std::mutex mut;

  /** Copy of the received pong message.  */
  std::unique_ptr<PongMessage> pongMessage;

  /** Resource of the sender of pong.  */
  std::string pongResource;

  /**
   * The SupportedNotifications stanza that was present on the last received
   * pong message (if any).
   */
  std::unique_ptr<SupportedNotifications> pongNotifications;

  void
  handlePresence (const gloox::Presence& p) override
  {
    VLOG (1) << "Processing presence from " << p.from ().full ();

    if (p.subtype () != gloox::Presence::Available)
      {
        LOG (INFO)
            << "Ignoring non-available presence from " << p.from ().full ();
        return;
      }

    auto* ext = p.findExtension (PongMessage::EXT_TYPE);
    if (ext != nullptr)
      {
        LOG (INFO) << "Received pong from " << p.from ().full ();

        auto* n = p.findExtension (SupportedNotifications::EXT_TYPE);
        if (n == nullptr)
          pongNotifications.reset ();
        else
          pongNotifications.reset (dynamic_cast<SupportedNotifications*> (
              n->clone ()));

        std::lock_guard<std::mutex> lock(mut);
        pongResource = p.from ().resource ();
        pongMessage.reset (dynamic_cast<PongMessage*> (ext->clone ()));
        CHECK (pongMessage->IsValid ());
        cv.notify_all ();
      }
  }

protected:

  ServerPingTests ()
  {
    RunWithClient ([this] (gloox::Client& c)
      {
        c.registerPresenceHandler (this);
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
    while (pongMessage == nullptr)
      cv.wait (lock);

    CHECK (!pongResource.empty ());
    return pongResource;
  }

  /**
   * Returns the SupportedNotifications stanza that was present on the last
   * pong message (or null if there was none).
   */
  const SupportedNotifications*
  GetNotifications () const
  {
    return pongNotifications.get ();
  }

  /**
   * Returns the PongMessage received.
   */
  const PongMessage&
  GetPongMessage () const
  {
    CHECK (pongMessage != nullptr);
    return *pongMessage;
  }

};

TEST_F (ServerPingTests, GetResourceAndVersion)
{
  SendPing (JIDWithoutResource (GetTestAccount (accServer)));
  EXPECT_EQ (WaitForPong (), SERVER_RES);
  EXPECT_EQ (GetPongMessage ().GetVersion (), SERVER_VERSION);
  EXPECT_EQ (GetNotifications (), nullptr);
}

TEST_F (ServerPingTests, SupportedNotifications)
{
  auto upd = UpdatableState::Create ();
  server.AddPubSub (GetServerConfig ().pubsub);
  server.AddNotification (upd->NewWaiter ("foo"));
  server.AddNotification (upd->NewWaiter ("bar"));

  SendPing (JIDWithoutResource (GetTestAccount (accServer)));
  EXPECT_EQ (WaitForPong (), SERVER_RES);

  const auto* n = GetNotifications ();
  ASSERT_NE (n, nullptr);
  EXPECT_EQ (n->GetService (), GetServerConfig ().pubsub);
  EXPECT_THAT (n->GetNotifications (), ElementsAre (
    std::make_pair ("bar", GetNotificationNode ("bar")),
    std::make_pair ("foo", GetNotificationNode ("foo"))
  ));
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

    const gloox::JID jidTo = JIDWithResource (GetTestAccount (accServer),
                                              SERVER_RES);
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

/**
 * Handler for receiving update notifications published by the server and
 * entering them into a synchronised queue (so we can expect to receive them).
 *
 * The test JSON for updatable states is transformed to strings of the form
 * "id=value" for the receiver queue.
 */
class NotificationReceiver : public ReceivedMessages
{

private:

  /** Notification type we expect.  */
  const std::string type;

  /**
   * Handles a received update.
   */
  void
  HandleUpdate (const gloox::Tag& t)
  {
    const auto* updTag = t.findChild ("update");
    if (updTag == nullptr)
      {
        LOG (WARNING)
            << "Ignoring update without our payload:\n" << t.xml ();
        return;
      }

    const NotificationUpdate upd(*updTag);
    if (!upd.IsValid ())
      {
        LOG (WARNING)
            << "Ignoring invalid payload update:\n" << t.xml ();
        return;
      }

    ASSERT_EQ (upd.GetType (), type);

    const std::string id = upd.GetState ()["id"].asString ();
    const std::string value = upd.GetState ()["value"].asString ();

    Add (id + "=" + value);
  }

public:

  /**
   * Creates a new receiver, which listens to the given node on the
   * given XMPP client.  All received notifications must be for the given
   * type of notification.
   */
  explicit NotificationReceiver (XmppClient& c, const std::string& t,
                                 const std::string& node)
    : type(t)
  {
    CHECK (c.GetPubSub ().SubscribeToNode (node, [this] (const gloox::Tag& t)
      {
        HandleUpdate (t);
      }));
  }

};

/**
 * Test case for the update notifications sent by a server.
 */
class ServerNotificationTests : public ServerTests
{

protected:

  ServerNotificationTests ()
  {
    AddPubSub (gloox::JID (GetServerConfig ().pubsub));
    server.AddPubSub (GetServerConfig ().pubsub);
  }

};

TEST_F (ServerNotificationTests, TwoNodes)
{
  auto s1 = UpdatableState::Create ();
  auto s2 = UpdatableState::Create ();
  server.AddNotification (s1->NewWaiter ("foo"));
  server.AddNotification (s2->NewWaiter ("bar"));

  NotificationReceiver r1(*this, "foo", GetNotificationNode ("foo"));
  NotificationReceiver r2(*this, "bar", GetNotificationNode ("bar"));

  s1->SetState ("a", "1");
  s2->SetState ("b", "2");
  r1.Expect ({"a=1"});
  r2.Expect ({"b=2"});

  s1->SetState ("c", "3");
  s2->SetState ("d", "4");
  r1.Expect ({"c=3"});
  r2.Expect ({"d=4"});
}

TEST_F (ServerNotificationTests, Reconnect)
{
  auto s = UpdatableState::Create ();
  server.AddNotification (s->NewWaiter ("foo"));

  server.Disconnect ();
  server.Connect (0);

  NotificationReceiver r(*this, "foo", GetNotificationNode ("foo"));

  s->SetState ("a", "1");
  r.Expect ({"a=1"});

  s->SetState ("b", "2");
  r.Expect ({"b=2"});
}

TEST_F (ServerNotificationTests, MultipleUpdates)
{
  auto s = UpdatableState::Create ();
  server.AddNotification (s->NewWaiter ("foo"));
  NotificationReceiver r(*this, "foo", GetNotificationNode ("foo"));

  s->SetState ("a", "1");
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  s->SetState ("b", "2");
  std::this_thread::sleep_for (std::chrono::milliseconds (50));
  s->SetState ("c", "3");

  r.Expect ({"a=1", "b=2", "c=3"});
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
