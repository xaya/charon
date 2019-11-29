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

#ifndef CHARON_SERVER_HPP
#define CHARON_SERVER_HPP

#include "rpcserver.hpp"

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

  /** The backing RpcServer instance.  */
  RpcServer& backend;

  /**
   * The XMPP client instance (which is a class defined only in the source
   * file and thus referenced here by pointer).
   */
  std::unique_ptr<IqAnsweringClient> client;

public:

  explicit Server (RpcServer& b);
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
   * Disconnects the XMPP client and stops processing requests.
   */
  void Disconnect ();

};

} // namespace charon

#endif // CHARON_SERVER_HPP
