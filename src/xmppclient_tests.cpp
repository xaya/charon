/*
    Charon - a transport system for GSP data
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include "xmppclient.hpp"

#include "testutils.hpp"

#include <gloox/message.h>
#include <gloox/messagehandler.h>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <vector>

namespace charon
{
namespace
{

/* ************************************************************************** */

/**
 * XMPP client connection for use in testing.  It can send messages and
 * receive them (passing on to a ReceivedMessages instance).
 */
class TestXmppClient : public XmppClient, private gloox::MessageHandler
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

    SetRootCA (GetTestCA ());

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
  TestXmppClient client(GetTestAccount (0));
  EXPECT_TRUE (client.IsConnected ());
}

TEST_F (XmppClientTests, FailedConnection)
{
  const auto& acc = GetTestAccount (0);

  XmppClient wrongPassword(JIDWithoutResource (acc), "wrong");
  wrongPassword.SetRootCA (GetTestCA ());
  EXPECT_FALSE (wrongPassword.Connect (0));
  EXPECT_FALSE (wrongPassword.IsConnected ());

  XmppClient invalidServer(gloox::JID ("test@invalid.server"), "password");
  wrongPassword.SetRootCA (GetTestCA ());
  EXPECT_FALSE (invalidServer.Connect (0));
  EXPECT_FALSE (invalidServer.IsConnected ());

  if (GetServerConfig ().server == std::string ("localhost"))
    {
      XmppClient invalidCert(JIDWithoutResource (acc), acc.password);
      /* We do not set any explicit CA file, which means that the system
         default will be used.  And that will make the localhost test server
         with a self-signed certificate fail.  */
      EXPECT_FALSE (invalidCert.Connect (0));
      EXPECT_FALSE (invalidCert.IsConnected ());
    }
  else
    LOG (WARNING)
        << "Skipping test for invalid TLS certificate"
        << " on a server that is not localhost";
}

TEST_F (XmppClientTests, Reconnection)
{
  TestXmppClient client(GetTestAccount (0));
  ASSERT_TRUE (client.IsConnected ());

  client.Disconnect ();
  ASSERT_FALSE (client.IsConnected ());

  EXPECT_TRUE (client.Connect (0));
  ASSERT_TRUE (client.IsConnected ());
}

TEST_F (XmppClientTests, Messages)
{
  TestXmppClient client1(GetTestAccount (0));
  TestXmppClient client2(GetTestAccount (1));

  client1.SendMessage (client2, "foo");
  client1.SendMessage (client2, "bar");

  client2.SendMessage (client1, "baz");

  client1.ExpectMessages ({"baz"});
  client2.ExpectMessages ({"foo", "bar"});
}

/* ************************************************************************** */

} // anonymous namespace
} // namespace charon
