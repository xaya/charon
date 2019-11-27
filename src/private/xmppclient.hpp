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

#ifndef CHARON_XMPPCLIENT_HPP
#define CHARON_XMPPCLIENT_HPP

#include <gloox/client.h>
#include <gloox/connectionlistener.h>
#include <gloox/loghandler.h>

namespace charon
{

/**
 * Basic XMPP client, based on the gloox library.  It manages the connection
 * and logs, but does not have any specific listening or other logic by itself.
 */
class XmppClient : private gloox::ConnectionListener, private gloox::LogHandler
{

private:

  /** Our JID for the connection.  */
  gloox::JID jid;

  /** The gloox XMPP client instance.  */
  gloox::Client client;

  /**
   * Set to true from the connection listener when we are connected.  When
   * disconnected, this is set back to false.
   */
  bool connected = false;

  void onConnect () override;
  void onDisconnect (gloox::ConnectionError err) override;
  bool onTLSConnect (const gloox::CertInfo& info) override;

  void handleLog (gloox::LogLevel level, gloox::LogArea area,
                  const std::string& msg) override;

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

  /**
   * Sets up the connection to the server, using the specified priority.
   */
  void Connect (int priority);

  /**
   * Polls the server for new messages.  This should be called regularly
   * by some event loop to ensure we keep updated.  Returns false if the
   * connection has been closed.
   */
  bool Receive ();

  /**
   * Closes the server connection.
   */
  void Disconnect ();

  /**
   * Returns the JID of the connected user.
   */
  const gloox::JID&
  GetJID () const
  {
    return jid;
  }

  /**
   * Returns the gloox client instance, which can be used to gsend messages
   * and other things.
   */
  gloox::Client&
  GetClient ()
  {
    return client;
  }

};

} // namespace charon

#endif // CHARON_XMPPCLIENT_HPP
