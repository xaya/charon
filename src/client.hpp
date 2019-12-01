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

#ifndef CHARON_CLIENT_HPP
#define CHARON_CLIENT_HPP

#include "rpcserver.hpp"

#include <json/json.h>

#include <chrono>
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
   * JID for reply.
   */
  explicit Client (const std::string& srv);

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
   * Connects to XMPP with the given JID and password.  Starts a thread
   * that processes any data we receive.
   */
  void Connect (const std::string& jid, const std::string& password,
                int priority);

  /**
   * Disconnects the XMPP client and stops processing data.
   */
  void Disconnect ();

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

};

} // namespace charon

#endif // CHARON_CLIENT_HPP
