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

#ifndef CHARON_CLIENT_HPP
#define CHARON_CLIENT_HPP

#include "notifications.hpp"
#include "rpcserver.hpp"

#include <json/json.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace charon
{

/**
 * The client-side logic of Charon.  An instance of this class manages an
 * XMPP connection, and is able to forward RPC requests to the server-side
 * XMPP user.
 */
class Client
{

private:

  class Impl;

  /** Duration type used for timeouts.  */
  using Duration = std::chrono::milliseconds;

  /** The user-chosen server JID, which is typically just a bare JID.  */
  std::string serverJid;

  /** The expected server version string.  */
  std::string version;

  /** Current timeout when waiting for replies of the server JID.  */
  Duration timeout;

  /**
   * The class implementing the main logic.  Its internals depend on private
   * libraries like gloox, so that the definition is not exposed in the header.
   */
  std::unique_ptr<Impl> impl;

public:

  /**
   * Constructs the client instance (without connecting it).  Any requests
   * made (after the instance is connected) will be forwarded to the given
   * JID for reply.#
   *
   * This already fixes the JID and password for the client's XMPP connection
   * as well, but does not yet actually connect to the XMPP network.
   */
  explicit Client (const std::string& srv, const std::string& v,
                   const std::string& jid, const std::string& password);

  ~Client ();

  Client () = delete;
  Client (const Client&) = delete;
  void operator= (const Client&) = delete;

  /**
   * Sets the timeout that we use when handling a forwarded call and waiting
   * for replies from the server JID.
   */
  template <typename Rep, typename Period>
    void
    SetTimeout (const std::chrono::duration<Rep, Period>& t)
  {
    timeout = std::chrono::duration_cast<Duration> (t);
  }

  /**
   * Connects to XMPP and starts a thread that processes any data we receive.
   */
  void Connect (int priority);

  /**
   * Disconnects the XMPP client and stops processing data.
   */
  void Disconnect ();

  /**
   * Adds a new notification type that we are interested in.  This must only be
   * called before the client is connected.
   */
  void AddNotification (std::unique_ptr<NotificationType> n);

  /**
   * Tries to find a full server JID if there is not already one.  This
   * performs the initial ping/pong handshake if not already done.
   * Returns the server's resource if one could be found or an empty string
   * if this failed.
   *
   * ForwardMethod selects a server by itself as needed, but this method
   * can be used for testing and also to explicitly perform the handshake
   * before starting to use the object and thus avoid future delays.
   */
  std::string GetServerResource ();

  /**
   * Forwards the given RPC call to the server for replying, and returns
   * the server's JSON-RPC result.  In case of error, throws RpcServer::Error.
   * This can be called concurrently from multiple threads and the processing
   * will be properly synchronised.
   */
  Json::Value ForwardMethod (const std::string& method,
                             const Json::Value& params);

  /**
   * Waits for a state change of the given notification.  Returns immediately
   * if the passed-in known state does not match the actual current state.
   * The method will also return eventually even if no state change has
   * happened.
   *
   * If no state is known yet and the call times out before one becomes
   * published by the server, this function may also return JSON null.
   */
  Json::Value WaitForChange (const std::string& type, const Json::Value& known);

};

} // namespace charon

#endif // CHARON_CLIENT_HPP
