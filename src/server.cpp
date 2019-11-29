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

#include "server.hpp"

#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"

#include <gloox/iq.h>
#include <gloox/iqhandler.h>
#include <gloox/message.h>
#include <gloox/messagehandler.h>

#include <glog/logging.h>

namespace charon
{

/**
 * The actual working class for our Charon server.  This uses XmppClient for
 * the actual XMPP connection, and listens for incoming IQ requests.
 */
class Server::IqAnsweringClient : public XmppClient,
                                  private gloox::MessageHandler,
                                  private gloox::IqHandler
{

private:

  /** The backend server to use for answering requests.  */
  RpcServer& backend;

  void handleMessage (const gloox::Message& msg,
                      gloox::MessageSession* session) override;
  bool handleIq (const gloox::IQ& iq) override;
  void handleIqID (const gloox::IQ& iq, int context) override;

public:

  explicit IqAnsweringClient (RpcServer& b, const gloox::JID& jid,
                              const std::string& password);

};

Server::IqAnsweringClient::IqAnsweringClient (RpcServer& b,
                                              const gloox::JID& jid,
                                              const std::string& password)
  : XmppClient(jid, password), backend(b)
{
  RunWithClient ([this] (gloox::Client& c)
    {
      c.registerStanzaExtension (new RpcRequest ());
      c.registerStanzaExtension (new RpcResponse ());
      c.registerStanzaExtension (new PingMessage ());
      c.registerStanzaExtension (new PongMessage ());

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

      gloox::Message response(gloox::Message::Normal, msg.from ());
      response.addExtension (new PongMessage ());

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

Server::Server (RpcServer& b)
  : backend(b)
{}

Server::~Server () = default;

void
Server::Connect (const std::string& jidStr, const std::string& password,
                 const int priority)
{
  const gloox::JID jid(jidStr);
  client = std::make_unique<IqAnsweringClient> (backend, jid, password);
  client->Connect (priority);
}

void
Server::Disconnect ()
{
  if (client == nullptr)
    return;

  client->Disconnect ();
  client.reset ();
}

} // namespace charon
