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

#ifndef CHARON_XMPPCLIENT_HPP
#define CHARON_XMPPCLIENT_HPP

#include <gloox/client.h>
#include <gloox/connectionlistener.h>
#include <gloox/loghandler.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace charon
{

class PubSubImpl;

/**
 * Basic XMPP client, based on the gloox library.  It manages the connection
 * and logs, but does not have any specific listening or other logic by itself.
 *
 * It also takes care of running a separate thread that listens on the XMPP
 * stream, and synchronising other requests to access the XMPP client (e.g.
 * send messages).
 */
class XmppClient : private gloox::ConnectionListener, private gloox::LogHandler
{

private:

  /**
   * Current state of the client.
   */
  enum class ConnectionState
  {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
  };

  /** Our JID for the connection.  */
  gloox::JID jid;

  /** The gloox XMPP client instance.  */
  gloox::Client client;

  /** PubSub instance used by this client.  */
  std::unique_ptr<PubSubImpl> pubsub;

  /**
   * When connected, this is the thread running polling for new messages.
   */
  std::unique_ptr<std::thread> recvLoop;

  /** Signal for the receive loop to stop.  */
  std::atomic<bool> stopLoop;

  /** Current connection state (set by the onConnect/onDisconnect handlers).  */
  std::atomic<ConnectionState> connectionState;

  /**
   * Lock used to synchronise receives and other client accesses.  This has
   * to be recursive so that also callbacks triggered in reply to a message
   * received during Receive() can again lock the mutex through RunWithClient
   * (e.g. if they want to send a reply stanza).
   */
  std::recursive_mutex mut;

  /**
   * Checks if there are new XMPP messages to process.  This is what the
   * receive thread calls repeatedly.
   */
  bool Receive ();

  void onConnect () override;
  void onDisconnect (gloox::ConnectionError err) override;
  bool onTLSConnect (const gloox::CertInfo& info) override;

  void handleLog (gloox::LogLevel level, gloox::LogArea area,
                  const std::string& msg) override;

  friend class PubSubImpl;

public:

  /**
   * Constructs the client based on the JID string (user@server/resource)
   * and the password to use.
   */
  explicit XmppClient (const gloox::JID& j, const std::string& password);

  /**
   * Closes the server connection in the destructor to ensure clean shutdown.
   */
  virtual ~XmppClient ();

  XmppClient () = delete;
  XmppClient (const XmppClient&) = delete;
  void operator= (const XmppClient&) = delete;

  /**
   * Adds a pubsub handler for the given pubsub service JID.
   */
  void AddPubSub (const gloox::JID& service);

  /**
   * Gives access to the pubsub instance.  Must only be called if one was
   * initialised with AddPubSub already.
   */
  PubSubImpl& GetPubSub ();

  /**
   * Sets up the connection to the server, using the specified priority.
   * Once connected, the receiving loop will be started.  The loop will
   * run until the connection is closed.
   *
   * Returns true if the connection was opened successfully, and false
   * if the connection failed (in which case the client will still be
   * disconnected after return).
   */
  bool Connect (int priority);

  /**
   * Closes the server connection.
   */
  void Disconnect ();

  /**
   * Returns true if the client is successfully connected.
   */
  bool
  IsConnected () const
  {
    return connectionState == ConnectionState::CONNECTED;
  }

  /**
   * Returns the JID of the connected user.
   */
  const gloox::JID&
  GetJID () const
  {
    return jid;
  }

  /**
   * Runs a provided callback with access to the underlying gloox Client,
   * synchronised with the receive loop.
   */
  template <typename Fcn>
    void
    RunWithClient (const Fcn& cb)
  {
    std::lock_guard<std::recursive_mutex> lock(mut);
    cb (client);
  }

};

} // namespace charon

#endif // CHARON_XMPPCLIENT_HPP
