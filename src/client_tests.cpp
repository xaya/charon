// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "client.hpp"

#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"
#include "testutils.hpp"

#include <gloox/message.h>
#include <gloox/messagehandler.h>

#include <gtest/gtest.h>

#include <glog/logging.h>

#include <chrono>
#include <string>
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

} // anonymous namespace
} // namespace charon
