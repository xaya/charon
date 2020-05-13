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

#include <gloox/iq.h>
#include <gloox/iqhandler.h>
#include <gloox/message.h>
#include <gloox/messagehandler.h>
#include <gloox/presence.h>

#include <glog/logging.h>

#include <map>

/* Windows systems define a GetMessage macro, which makes this file fail to
   compile because of JsonRpcException::GetMessage.  We cannot rename the
   method itself, as it comes from libjson-rpc-cpp.  */
#undef GetMessage

namespace charon
{

namespace
{

/**
 * An enabled notification on the server.  This mostly wraps the corresponding
 * WaiterThread instance, but also has some more data like the pubsub node's
 * name for updates.
 */
class ServerNotification
{

private:

  /** The PubSubImpl we use to send notifications.  */
  PubSubImpl& pubsub;

  /** The PubSub node name.  */
  std::string node;

  /** The underlying WaiterThread doing most of the work.  */
  std::unique_ptr<WaiterThread> thread;

public:

  /**
   * Constructs a new instance, taking over the existing WaiterThread.  This
   * also sets everything else up (pubsub node, update handler) and starts
   * the waiter thread.
   */
  explicit ServerNotification (PubSubImpl& p, std::unique_ptr<WaiterThread> t);

  /**
   * Stops the waiter thread and cleans everything up.
   */
  ~ServerNotification ();

  ServerNotification () = delete;
  ServerNotification (const ServerNotification&) = delete;
  void operator= (const ServerNotification&) = delete;

  /**
   * Returns the associated node string.
   */
  const std::string&
  GetNode () const
  {
    return node;
  }

};

ServerNotification::ServerNotification (PubSubImpl& p,
                                        std::unique_ptr<WaiterThread> t)
  : pubsub(p), thread(std::move (t))
{
  node = pubsub.CreateNode ();
  LOG (INFO)
      << "Serving notifications for " << thread->GetType ()
      << " on PubSub node " << node;

  thread->SetUpdateHandler ([this] (const Json::Value& data)
    {
      VLOG (1)
          << "Notifying update for " << thread->GetType ()
          << ":\n" << data;

      const NotificationUpdate payload(thread->GetType (), data);
      pubsub.Publish (node, payload.CreateTag ());
    });

  thread->Start ();
}

ServerNotification::~ServerNotification ()
{
  thread->Stop ();
}

} // anonymous namespace

/**
 * The actual working class for our Charon server.  This uses XmppClient for
 * the actual XMPP connection, and listens for incoming IQ requests.
 */
class Server::IqAnsweringClient : public XmppClient,
                                  private gloox::MessageHandler,
                                  private gloox::IqHandler
{

private:

  /** The server's version string.  */
  const std::string& version;

  /** The backend server to use for answering requests.  */
  RpcServer& backend;

  /** Active notification updaters.  They are keyed by type string.  */
  std::map<std::string, std::unique_ptr<ServerNotification>> notifications;

  void handleMessage (const gloox::Message& msg,
                      gloox::MessageSession* session) override;
  bool handleIq (const gloox::IQ& iq) override;
  void handleIqID (const gloox::IQ& iq, int context) override;

protected:

  /**
   * When disconnected, we clean up our notifications.
   */
  void HandleDisconnect () override;

public:

  explicit IqAnsweringClient (const std::string& v, RpcServer& b,
                              const gloox::JID& jid,
                              const std::string& password);

  /**
   * Removes all notifications (as preparation to shutting down).
   */
  void
  ClearNotifications ()
  {
    notifications.clear ();
  }

  /**
   * Adds a new notification updater and returns the pubsub node.
   */
  std::string AddNotification (std::unique_ptr<WaiterThread> upd);

};

Server::IqAnsweringClient::IqAnsweringClient (const std::string& v,
                                              RpcServer& b,
                                              const gloox::JID& jid,
                                              const std::string& password)
  : XmppClient(jid, password), version(v), backend(b)
{
  RunWithClient ([this] (gloox::Client& c)
    {
      c.registerStanzaExtension (new RpcRequest ());
      c.registerStanzaExtension (new RpcResponse ());
      c.registerStanzaExtension (new PingMessage ());
      c.registerStanzaExtension (new PongMessage ());
      c.registerStanzaExtension (new SupportedNotifications ());

      c.registerMessageHandler (this);
      c.registerIqHandler (this, RpcRequest::EXT_TYPE);
    });
}

void
Server::IqAnsweringClient::handleMessage (const gloox::Message& msg,
                                          gloox::MessageSession* session)
{
  VLOG (1) << "Received message stanza from " << msg.from ().full ();

  auto* ping = msg.findExtension<PingMessage> (PingMessage::EXT_TYPE);
  if (ping != nullptr)
    {
      LOG (INFO) << "Processing ping from " << msg.from ().full ();

      gloox::Presence response(gloox::Presence::Available, msg.from ());
      response.addExtension (new PongMessage (version));

      if (!notifications.empty ())
        {
          const auto service = GetPubSub ().GetService ().full ();
          auto notificationInfo
              = std::make_unique<SupportedNotifications> (service);

          for (const auto& entry : notifications)
            notificationInfo->AddNotification (entry.first,
                                               entry.second->GetNode ());

          response.addExtension (notificationInfo.release ());
        }

      RunWithClient ([&response] (gloox::Client& c)
        {
          c.send (response);
        });
    }
}

bool
Server::IqAnsweringClient::handleIq (const gloox::IQ& iq)
{
  LOG (INFO) << "Received IQ request from " << iq.from ().full ();

  auto* req = iq.findExtension<RpcRequest> (RpcRequest::EXT_TYPE);

  /* The handler should only be called by gloox if it detects the extension,
     since that's how we registered it.  */
  CHECK (req != nullptr) << "IQ has no RpcRequest extension";

  if (!req->IsValid ())
    {
      LOG (WARNING) << "Ignoring invalid RpcRequest stanza";
      return false;
    }

  if (iq.subtype () != gloox::IQ::Get)
    {
      LOG (WARNING) << "Ignoring IQ of type " << iq.subtype ();
      return false;
    }

  std::unique_ptr<RpcResponse> result;
  try
    {
      const Json::Value resultJson = backend.HandleMethod (req->GetMethod (),
                                                           req->GetParams ());
      result = std::make_unique<RpcResponse> (resultJson);
    }
  catch (const RpcServer::Error& exc)
    {
      result = std::make_unique<RpcResponse> (exc.GetCode (), exc.GetMessage (),
                                              exc.GetData ());
    }

  /* We always return an IQ type of result, even if we have a JSON-RPC error.
     This mimics best practices for JSON-RPC over HTTP, where "error" is
     only returned for transport-related errors.  If the XMPP IQ itself was
     fine but the call failed, we return an IQ result with an embedded
     JSON-RPC error response.  */
  gloox::IQ response(gloox::IQ::Result, iq.from (), iq.id ());
  response.addExtension (result.release ());

  RunWithClient ([&response] (gloox::Client& c)
    {
      c.send (response);
    });

  return true;
}

void
Server::IqAnsweringClient::handleIqID (const gloox::IQ& iq, const int context)
{}

std::string
Server::IqAnsweringClient::AddNotification (std::unique_ptr<WaiterThread> upd)
{
  const auto type = upd->GetType ();

  auto notifier
      = std::make_unique<ServerNotification> (GetPubSub (), std::move (upd));
  const auto node = notifier->GetNode ();

  const auto res = notifications.emplace (type, std::move (notifier));
  CHECK (res.second) << "Duplicate notification: " << type;

  return node;
}

void
Server::IqAnsweringClient::HandleDisconnect ()
{
  ClearNotifications ();
}

Server::Server (const std::string& v, RpcServer& b)
  : version(v), backend(b)
{}

Server::~Server () = default;

void
Server::Connect (const std::string& jidStr, const std::string& password,
                 const int priority)
{
  const gloox::JID jid(jidStr);
  client = std::make_unique<IqAnsweringClient> (version, backend,
                                                jid, password);
  client->Connect (priority);
}

void
Server::AddPubSub (const std::string& service)
{
  CHECK (client != nullptr);
  CHECK (!hasPubSub);

  const gloox::JID serviceJid(service);
  client->AddPubSub (serviceJid);

  hasPubSub = true;
}

void
Server::Disconnect ()
{
  if (client == nullptr)
    return;

  client->Disconnect ();
  client.reset ();
}

std::string
Server::AddNotification (std::unique_ptr<WaiterThread> upd)
{
  CHECK (client != nullptr);
  CHECK (hasPubSub);
  return client->AddNotification (std::move (upd));
}

} // namespace charon
