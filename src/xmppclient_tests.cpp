// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/xmppclient.hpp"

#include "testutils.hpp"

#include <gloox/message.h>
#include <gloox/messagehandler.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <glog/logging.h>

#include <condition_variable>
#include <mutex>
#include <vector>

namespace charon
{
namespace
{

using testing::IsEmpty;

/* ************************************************************************** */

/**
 * A queue of received messages, including a mechanism to synchronise adding
 * to the queue and waiting for expected messages to arrive.
 */
class ReceivedMessages
{

private:

  /** The messages themselves, as received in order.  */
  std::vector<std::string> messages;

  /**
   * Mutex for synchronising the receiving messages and waiting for them
   * when comparing to expectations.
   */
  std::mutex mut;

  /** Condition variable for waiting for more messages.  */
  std::condition_variable cv;

public:

  ReceivedMessages () = default;

  ~ReceivedMessages ()
  {
    EXPECT_THAT (messages, IsEmpty ()) << "Unexpected messages received";
  }

  /**
   * Adds a newly received message to the queue and potentially signals
   * waiting threads.
   */
  void
  Add (const std::string& msg)
  {
    std::lock_guard<std::mutex> lock(mut);
    messages.push_back (msg);
    cv.notify_all ();
  }

  /**
   * Expects to receive the given messages in order.  Waits for them to
   * arrive as needed, and clears out the message queue at the end.
   */
  void
  Expect (const std::vector<std::string>& expected)
  {
    std::unique_lock<std::mutex> lock(mut);
    while (messages.size () < expected.size ())
      {
        LOG (INFO) << "Waiting for more messages to be received...";
        cv.wait (lock);
      }

    EXPECT_EQ (messages, expected);
    messages.clear ();
  }

};

/**
 * XMPP client connection for use in testing.  It can send messages and
 * receive them (passing on to a ReceivedMessages instance).
 */
class TestXmppClient : private XmppClient, private gloox::MessageHandler
{

private:

  ReceivedMessages messages;

  void
  handleMessage (const gloox::Message& msg,
                 gloox::MessageSession* session) override
  {
    VLOG (1)
        << "Received XMPP message from " << msg.from ().full () << ":\n"
        << msg.body ();
    messages.Add (msg.body ());
  }

public:

  explicit TestXmppClient (const TestAccount& acc)
    : XmppClient(JIDWithoutResource (acc), acc.password)
  {
    RunWithClient ([this] (gloox::Client& c)
      {
        c.registerMessageHandler (this);
      });

    /* Connect with the default priority of zero.  */
    Connect (0);
  }

  /**
   * Expects that the received messages match the given ones.  This resets
   * the received messages.
   */
  void
  ExpectMessages (const std::vector<std::string>& expected)
  {
    messages.Expect (expected);
  }

  /**
   * Sends a message as normal chat message.
   */
  void
  SendMessage (const TestXmppClient& to, const std::string& body)
  {
    const gloox::JID jidTo = to.GetJID ();
    LOG (INFO) << "Sending to " << jidTo.full () << ":\n" << body;

    gloox::Message msg(gloox::Message::Chat, jidTo, body);
    RunWithClient ([this, &msg] (gloox::Client& c)
      {
        c.send (msg);
      });
  }

};

/* ************************************************************************** */

using XmppClientTests = testing::Test;

TEST_F (XmppClientTests, Connecting)
{
  TestXmppClient client(ACCOUNTS[0]);
}

TEST_F (XmppClientTests, Messages)
{
  TestXmppClient client1(ACCOUNTS[0]);
  TestXmppClient client2(ACCOUNTS[1]);

  client1.SendMessage (client2, "foo");
  client1.SendMessage (client2, "bar");

  client2.SendMessage (client1, "baz");

  client1.ExpectMessages ({"baz"});
  client2.ExpectMessages ({"foo", "bar"});
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
