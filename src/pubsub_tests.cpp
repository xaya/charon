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

#include "private/pubsub.hpp"

#include "private/xmppclient.hpp"

#include "testutils.hpp"

#include <gloox/tag.h>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <vector>

namespace charon
{
namespace
{

/* ************************************************************************** */

/**
 * Simple wrapper around XmppClient that automatically logs into a test account
 * and enables pubsub.  Also incorporates a receiver for items.
 */
class PubSubClient : public XmppClient
{

private:

  ReceivedMessages recv;

public:

  explicit PubSubClient (const TestAccount& acc, const std::string res = "")
    : XmppClient(JIDWithResource (acc, res), acc.password)
  {
    Connect (0);
    AddPubSub (gloox::JID (PUBSUB_SERVICE));
  }

  /**
   * Tries to subscribe to the given node.  This uses a callback that
   * just puts received item payloads into the queue of received messages.
   */
  bool
  Subscribe (const std::string& node)
  {
    const auto cb = [this] (const gloox::Tag& t)
      {
        ASSERT_EQ (t.children ().size (), 1);
        recv.Add ((*t.children ().begin ())->xml ());
      };

    return GetPubSub ().SubscribeToNode (node, cb);
  }

  /**
   * Expects the given list of inner XML strings.
   */
  void
  ExpectItems (const std::vector<std::string>& expected)
  {
    recv.Expect (expected);
  }

  /**
   * Publishes a tag with the given name and inner text data.  Returns the
   * tag's XML, which is what the NotificationReceiver will need.
   */
  std::string
  Publish (const std::string& node, const std::string& name,
           const std::string& text)
  {
    auto t = std::make_unique<gloox::Tag> (name, text);
    const std::string res = t->xml ();
    GetPubSub ().Publish (node, std::move (t));
    return res;
  }

};

/**
 * Test case with two XMPP clients and pubsub between them.
 */
class PubSubTests : public testing::Test
{

protected:

  PubSubClient client;
  PubSubClient server;

  PubSubTests ()
    : client(ACCOUNTS[0]), server(ACCOUNTS[1])
  {}

};

/* ************************************************************************** */

TEST_F (PubSubTests, CreateAndSubscribe)
{
  const auto node = server.GetPubSub ().CreateNode ();
  LOG (INFO) << "Created node: " << node;
  EXPECT_TRUE (client.Subscribe (node));
}

TEST_F (PubSubTests, SubscribeToNonExistantNode)
{
  EXPECT_FALSE (client.Subscribe ("node does not exist"));
}

TEST_F (PubSubTests, PublishReceive)
{
  const auto node = server.GetPubSub ().CreateNode ();
  ASSERT_TRUE (client.Subscribe (node));

  const auto xml1 = server.Publish (node, "mytag", "with some text");
  const auto xml2 = server.Publish (node, "othertag", "other text");

  client.ExpectItems ({xml1, xml2});
}

TEST_F (PubSubTests, SubscribeAfterFirstPublish)
{
  const auto node = server.GetPubSub ().CreateNode ();
  server.Publish (node, "mytag", "should not be received");

  ASSERT_TRUE (client.Subscribe (node));
  const auto xml = server.Publish (node, "othertag", "this is received");

  client.ExpectItems ({xml});

}

TEST_F (PubSubTests, TwoClients)
{
  const auto node = server.GetPubSub ().CreateNode ();
  ASSERT_TRUE (client.Subscribe (node));

  PubSubClient otherClient(ACCOUNTS[0]);
  ASSERT_TRUE (otherClient.Subscribe (node));

  const auto xml1 = server.Publish (node, "tag1", "first");
  const auto xml2 = server.Publish (node, "tag2", "second");

  client.ExpectItems ({xml1, xml2});
  otherClient.ExpectItems ({xml1, xml2});
}

TEST_F (PubSubTests, OneClientUnsubscribes)
{
  const auto node = server.GetPubSub ().CreateNode ();
  ASSERT_TRUE (client.Subscribe (node));

  std::string xml1;
  {
    PubSubClient otherClient(ACCOUNTS[0]);
    ASSERT_TRUE (otherClient.Subscribe (node));

    xml1 = server.Publish (node, "tag1", "first");
    otherClient.ExpectItems ({xml1});
  }

  const auto xml2 = server.Publish (node, "tag2", "second");
  client.ExpectItems ({xml1, xml2});
}

TEST_F (PubSubTests, ClientReconnectNotAutomaticallySubscribed)
{
  const auto node = server.GetPubSub ().CreateNode ();

  {
    PubSubClient otherClient(ACCOUNTS[0], "res");
    ASSERT_TRUE (otherClient.Subscribe (node));

    const auto xml = server.Publish (node, "tag1", "first");
    otherClient.ExpectItems ({xml});
  }

  /* Now reconnect with the same JID, but do not immediately subscribe to
     the node again.  Only after we explicitly subscribe should we receive
     published items again.  */
  {
    PubSubClient otherClient(ACCOUNTS[0], "res");
    server.Publish (node, "tag2", "second");

    ASSERT_TRUE (otherClient.Subscribe (node));
    const auto xml = server.Publish (node, "tag3", "third");

    otherClient.ExpectItems ({xml});
  }
}

TEST_F (PubSubTests, NodeGoesOffline)
{
  PubSubClient otherServer(ACCOUNTS[1]);
  const auto node = otherServer.GetPubSub ().CreateNode ();
  ASSERT_TRUE (client.Subscribe (node));
  /* It is fine that the node goes offline and is deleted before the
     client unsubscribes from it.  */
}

TEST_F (PubSubTests, ServerCleansUpNode)
{
  std::string node;
  {
    PubSubClient otherServer(ACCOUNTS[1]);
    node = otherServer.GetPubSub ().CreateNode ();
  }

  /* When cleaning up, the server only sends the request but does not wait
     on the results (to not delay the destructor).  Thus wait a bit manually
     to make sure the deletion has gone through.  */
  std::this_thread::sleep_for (std::chrono::milliseconds (500));

  EXPECT_FALSE (client.Subscribe (node));
}

TEST_F (PubSubTests, TwoServers)
{
  const auto node1 = server.GetPubSub ().CreateNode ();
  ASSERT_TRUE (client.Subscribe (node1));

  std::vector<std::string> xmls;
  {
    PubSubClient otherServer(ACCOUNTS[1]);
    const auto node2 = otherServer.GetPubSub ().CreateNode ();
    ASSERT_TRUE (client.Subscribe (node2));

    xmls.push_back (server.Publish (node1, "tag1", "first"));
    xmls.push_back (otherServer.Publish (node2, "tag2", "second"));
  }

  xmls.push_back (server.Publish (node1, "tag3", "third"));
  client.ExpectItems (xmls);
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
