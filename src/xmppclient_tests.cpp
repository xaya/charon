// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "private/xmppclient.hpp"

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

/** The XMPP server used for testing.  */
constexpr const char* XMPP_SERVER = "chat.xaya.io";

/**
 * Data for one of the test accounts that we use.
 */
struct TestAccount
{

  /** The username for the XMPP server.  */
  const char* name;

  /** The password for logging into the server.  */
  const char* password;

};

/**
 * Our test accounts.  They are all set up for XID on mainnet, and the address
 * CLkoEc3g1XCqF1yevLfE1F2EksLhGSd8GC is set as global signer.  It has the
 * private key LMeJqBHefZdZbH7pHBqhmBu3pFzBpVo78SnBgsoHc6KYaDv9CEYp.
 * The passwords given are unexpiring XID credentials for chat.xaya.io.
 */
constexpr TestAccount ACCOUNTS[] =
  {
    {
      "xmpptest1",
      "CkEfa5+WT2Rc5/TiMDhMynAbSJ+DY9FmE5lcWgWMRQWUBV5UQsgjiBWL302N4kdLZYygJVBV"
      "x3vYsDNUx8xBbw27WA==",
    },
    {
      "xmpptest2",
      "CkEgOEFNwRdLQ6uD543MJLSzip7mTahM1we9GDl3S5NlR49nrJ0JxcFfQmDbbF4C4OpqSlTp"
      "x8OG6xtFjCUMLh/AGA==",
    },
  };

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
 * Constructs the JID for a test account, without resource.
 */
gloox::JID
JIDWithoutResource (const TestAccount& acc)
{
  gloox::JID res;
  res.setUsername (acc.name);
  res.setServer (XMPP_SERVER);
  return res;
}

/**
 * XMPP client connection for use in testing.  It can send messages and
 * receive them (passing on to a ReceivedMessages instance).
 */
class TestXmppClient : private XmppClient, private gloox::MessageHandler
{

private:

  /** Received messages we pass to.  */
  ReceivedMessages messages;

  /** Thread running the receiving loop.  */
  std::unique_ptr<std::thread> loop;

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
    GetClient ().registerMessageHandler (this);
    Start ();
  }

  ~TestXmppClient ()
  {
    if (loop != nullptr)
      Stop ();
  }

  /**
   * Connects to the server and starts a loop receiving messages.
   */
  void
  Start ()
  {
    CHECK (loop == nullptr);

    /* Connect with the default priority of zero.  */
    Connect (0);
    loop = std::make_unique<std::thread> ([this] ()
      {
        while (Receive ());
      });
  }

  /**
   * Disconnects the XMPP client and stops the receiving loop.
   */
  void
  Stop ()
  {
    CHECK (loop != nullptr);
    Disconnect ();
    loop->join ();
    loop.reset ();
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
    VLOG (1) << "Sending to " << jidTo.full () << ":\n" << body;

    gloox::Message msg(gloox::Message::Chat, jidTo, body);
    GetClient ().send (msg);
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
