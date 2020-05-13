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

#ifndef CHARON_SERVER_HPP
#define CHARON_SERVER_HPP

#include "rpcserver.hpp"
#include "waiterthread.hpp"

#include <memory>
#include <string>

namespace charon
{

/**
 * The main Charon server:  This is the component that listens for IQ requests
 * over XMPP and answers them through some backing RpcServer instance.
 */
class Server
{

private:

  class IqAnsweringClient;

  /**
   * The XMPP client instance (which is a class defined only in the source
   * file and thus referenced here by pointer).
   */
  std::unique_ptr<IqAnsweringClient> client;

  /** Whether or not we have a pubsub service.  */
  bool hasPubSub = false;

  /**
   * Returns the pubsub node for a given notification type.  This is used
   * in tests.
   */
  const std::string& GetNotificationNode (const std::string& type) const;

  friend class ServerTests;

public:

  explicit Server (const std::string& version, RpcServer& backend,
                   const std::string& jid, const std::string& password);
  ~Server ();

  Server () = delete;
  Server (const Server&) = delete;
  void operator= (const Server&) = delete;

  /**
   * Adds a pubsub service that can be used for notifications on the XMPP
   * server we are connected to.
   */
  void AddPubSub (const std::string& service);

  /**
   * Starts serving a new notification on the server.  This must only be called
   * if we have a pubsub service enabled.
   *
   * If the client is connected already, then this enables the new notification
   * right away.  Otherwise the notification will be enabled once the client
   * gets connected (and later again if it reconnects).
   */
  void AddNotification (std::unique_ptr<WaiterThread> upd);

  /**
   * Connects to XMPP with the given priority.  Starts processing
   * requests once the connection is established.
   */
  void Connect (int priority);

  /**
   * Disconnects the XMPP client and stops processing requests.
   */
  void Disconnect ();

};

} // namespace charon

#endif // CHARON_SERVER_HPP
