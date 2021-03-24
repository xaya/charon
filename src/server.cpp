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

#include "server.hpp"

#include "private/pubsub.hpp"
#include "private/stanzas.hpp"
#include "xmppclient.hpp"

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

/* ************************************************************************** */

namespace
{

/**
 * An enabled notification on the server.  This mostly wraps the corresponding
 * WaiterThread instance, but also has some more data like the pubsub node's
 * name for updates if the server is connected to XMPP.
 */
class ServerNotification
{

private:

  /** The underlying WaiterThread doing most of the work.  */
  std::unique_ptr<WaiterThread> thread;

  /**
   * The PubSubImpl we use to send notifications or null if the
   * XMPP client is not connected and we are not sending out notifications
   * for now.
   */
  PubSubImpl* pubsub = nullptr;

  /** The PubSub node name (if any).  */
  std::string node;

  /**
   * Mutex to lock this between the waiter thread's update handler
   * and an external thread that may connect/disconnect the pubsub.
   */
  std::mutex mut;

public:

  /**
   * Constructs a new instance for the given WaiterThread.  This also sets
   * up the update handler and starts the waiter thread.
   */
  explicit ServerNotification (std::unique_ptr<WaiterThread> t);

  /**
   * Stops the waiter thread and cleans everything up.
   */
  ~ServerNotification ();

  ServerNotification () = delete;
  ServerNotification (const ServerNotification&) = delete;
  void operator= (const ServerNotification&) = delete;

  /**
   * Connects a PubSub implementation and starts publishing there.
   */
  void ConnectPubSub (PubSubImpl& p);

  /**
   * Disconnects the PubSub instance and stops publishing updates.
   */
  void DisconnectPubSub ();

  /**
   * Returns the associated node string.
   */
  const std::string&
  GetNode () const
  {
    CHECK (pubsub != nullptr) << "PubSub is not connected";
    return node;
  }

};

ServerNotification::ServerNotification (std::unique_ptr<WaiterThread> t)
  : thread(std::move (t))
{
  thread->SetUpdateHandler ([this] (const Json::Value& data)
    {
      VLOG (1)
          << "Notifying update for " << thread->GetType ()
          << ":\n" << data;

      /* Locking is a bit tricky here.  The Publish call below may block
         for some time, because it is waiting for the server response.
         If the XMPP client is disconnected in the mean time, the waiter
         is woken up on ~PubSubImpl.  But before that happens, the
         notification is disconnected from PubSub.

         All this only works if we do not hold the lock on mut while
         the Publish call is going on.  Thus we just lock while we copy
         the data we need, and then process the data without keeping
         onto the lock.  */

      PubSubImpl* p;
      std::string n;
      {
        std::lock_guard<std::mutex> lock(mut);
        p = pubsub;
        n = node;
      }

      if (p == nullptr)
        return;
      CHECK (!n.empty ());

      const NotificationUpdate payload(thread->GetType (), data);
      p->Publish (n, payload.CreateTag ());
    });

  thread->Start ();
}

ServerNotification::~ServerNotification ()
{
  thread->Stop ();
  thread->ClearUpdateHandler ();
}

void
ServerNotification::ConnectPubSub (PubSubImpl& p)
{
  std::lock_guard<std::mutex> lock(mut);

  CHECK (pubsub == nullptr) << "There is already a PubSub instance";
  pubsub = &p;

  node = pubsub->CreateNode ();
  LOG (INFO)
      << "Serving notifications for " << thread->GetType ()
      << " on PubSub node " << node;
}

void
ServerNotification::DisconnectPubSub ()
{
  std::lock_guard<std::mutex> lock(mut);

  pubsub = nullptr;
  node.clear ();

  LOG (INFO) << "Stopped PubSub updates for " << thread->GetType ();
}

} // anonymous namespace

/* ************************************************************************** */

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
  const std::string version;

  /** The backend server to use for answering requests.  */
  RpcServer& backend;

  /**
   * Enabled notifications on this server.  All of them have their waiter
   * threads running, but they may not be publishing to a PubSub instance
   * if the XMPP client is currently disconnected.
   */
  std::map<std::string, std::unique_ptr<ServerNotification>> notifications;

  /**
   * Set to true when all is fully set up and ready, i.e. once all notification
   * pubsubs have been set up.  Only then does the server reply to pings.
   */
  bool ready = false;

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
   * Adds a new notification updater.  This starts the corresponding waiter
   * thread immediately, but only starts publishing to a PubSub once the
   * client is connected.
   */
  void AddNotification (std::unique_ptr<WaiterThread> upd);

  /**
   * Connects all notifications to the current PubSub.  This is used to
   * explicitly enable them if the client has just been connected to XMPP.
   */
  void ConnectNotifications ();

  /**
   * Returns the pubsub node for the given notification type.  This is used
   * in testing.
   */
  const std::string& GetNotificationNode (const std::string& type) const;

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
      if (!ready)
        {
          LOG (WARNING)
              << "Server is not ready yet, ignoring ping from "
              << msg.from ().full ();
          return;
        }

      LOG (INFO) << "Processing ping from " << msg.from ().full ();

      gloox::Presence response(gloox::Presence::Available, msg.from ());
      response.addExtension (new PongMessage (version));

      if (!notifications.empty ())
        {
          const auto service = GetPubSub ().GetService ().full ();
          auto notificationInfo
              = std::make_unique<SupportedNotifications> (service);

          for (const auto& entry : notifications)
            {
              const auto& node = entry.second->GetNode ();
              notificationInfo->AddNotification (entry.first, node);
            }

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

void
Server::IqAnsweringClient::HandleDisconnect ()
{
  ready = false;
  for (auto& n : notifications)
    n.second->DisconnectPubSub ();
}

void
Server::IqAnsweringClient::AddNotification (std::unique_ptr<WaiterThread> upd)
{
  const auto type = upd->GetType ();

  auto notifier = std::make_unique<ServerNotification> (std::move (upd));
  if (IsConnected ())
    notifier->ConnectPubSub (GetPubSub ());

  const auto res = notifications.emplace (type, std::move (notifier));
  CHECK (res.second) << "Duplicate notification: " << type;
}

void
Server::IqAnsweringClient::ConnectNotifications ()
{
  for (auto& n : notifications)
    n.second->ConnectPubSub (GetPubSub ());
  ready = true;
}

const std::string&
Server::IqAnsweringClient::GetNotificationNode (const std::string& type) const
{
  return notifications.at (type)->GetNode ();
}

/* ************************************************************************** */

Server::Server (const std::string& version, RpcServer& backend,
                const std::string& jidStr, const std::string& password)
{
  const gloox::JID jid(jidStr);
  client = std::make_unique<IqAnsweringClient> (version, backend,
                                                jid, password);
}

Server::~Server () = default;

void
Server::AddPubSub (const std::string& service)
{
  CHECK (!hasPubSub);

  const gloox::JID serviceJid(service);
  client->AddPubSub (serviceJid);

  hasPubSub = true;
}

void
Server::AddNotification (std::unique_ptr<WaiterThread> upd)
{
  CHECK (hasPubSub);
  client->AddNotification (std::move (upd));
}

void
Server::SetRootCA (const std::string& path)
{
  client->SetRootCA (path);
}

bool
Server::Connect (const int priority)
{
  if (!client->Connect (priority))
    return false;

  client->ConnectNotifications ();
  return true;
}

void
Server::Disconnect ()
{
  client->Disconnect ();
}

bool
Server::IsConnected () const
{
  return client->IsConnected ();
}

const std::string&
Server::GetNotificationNode (const std::string& type) const
{
  return client->GetNotificationNode (type);
}

/* ************************************************************************** */

void
Server::ReconnectLoop::Start (const int priority)
{
  CHECK (loop == nullptr) << "ReconnectLoop is already running";

  shouldStop = false;
  loop = std::make_unique<std::thread> ([this, priority] ()
    {
      std::unique_lock<std::mutex> lock(mut);

      while (!shouldStop)
        {
          if (!srv.IsConnected ())
            srv.Connect (priority);

          cv.wait_for (lock, interval);
        }

      /* When the loop has been stopped, disconnect the server.  */
      if (srv.IsConnected ())
        srv.Disconnect ();
    });
}

void
Server::ReconnectLoop::Stop ()
{
  CHECK (loop != nullptr) << "ReconnectLoop is not running";

  {
    std::lock_guard<std::mutex> lock(mut);
    shouldStop = true;
    cv.notify_all ();
  }

  loop->join ();
  loop.reset ();
}

/* ************************************************************************** */

} // namespace charon
