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

  /** The version string to report.  */
  const std::string version;

  /** The backing RpcServer instance.  */
  RpcServer& backend;

  /**
   * The XMPP client instance (which is a class defined only in the source
   * file and thus referenced here by pointer).
   */
  std::unique_ptr<IqAnsweringClient> client;

  /** Whether or not we have a pubsub service.  */
  bool hasPubSub = false;

public:

  explicit Server (const std::string& v, RpcServer& b);
  ~Server ();

  Server () = delete;
  Server (const Server&) = delete;
  void operator= (const Server&) = delete;

  /**
   * Connects to XMPP with the given JID and password.  Starts processing
   * requests once the connection is established.
   */
  void Connect (const std::string& jid, const std::string& password,
                int priority);

  /**
   * Adds a pubsub service that can be used for notifications on the XMPP
   * server we are connected to.
   */
  void AddPubSub (const std::string& service);

  /**
   * Disconnects the XMPP client and stops processing requests.
   */
  void Disconnect ();

  /**
   * Starts serving a new notification on the server.  This must only be called
   * if we have a pubsub service enabled.
   *
   * Returns the pubsub node that has been created for updates on this
   * notification type.
   */
  std::string AddNotification (std::unique_ptr<WaiterThread> upd);

};

} // namespace charon

#endif // CHARON_SERVER_HPP
