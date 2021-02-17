/*
    Charon - a transport system for GSP data
    Copyright (C) 2021  Autonomous Worlds Ltd

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

#ifndef CHARON_UTILS_CLIENT_HPP
#define CHARON_UTILS_CLIENT_HPP

#include <memory>
#include <set>
#include <string>

namespace charon
{

/**
 * A simple wrapper around the full-fledged Charon client, which allows
 * to spin up a Charon client with local RPC interface easily.  This is used
 * by the charon-client utility binary, but can also be used from external
 * code as needed.
 */
class UtilClient
{

private:

  class Impl;

  /** The actual implementation (which is hidden from the header).  */
  std::unique_ptr<Impl> impl;

public:

  /**
   * Constructs a new client with the given base data.
   */
  explicit UtilClient (const std::string& serverJid,
                       const std::string& backendVersion,
                       const std::string& clientJid,
                       const std::string& password,
                       int port);

  ~UtilClient ();

  /**
   * Enables forwarding for the given list of methods.
   */
  void AddMethods (const std::set<std::string>& methods);

  /**
   * Turns on the waitforchange notification.
   */
  void EnableWaitForChange ();

  /**
   * Turns on the waitforpendingchange notification.
   */
  void EnableWaitForPendingChange ();

  /**
   * Runs the main loop, optionally detecting the server right away
   * (instead of just doing it as needed for RPC calls).  This connects
   * the Charon client, starts the local RPC server, and then blocks until
   * the server is shut down through RPC.
   *
   * If detectServer is true and the server detection fails, this throws
   * an exception.
   */
  void Run (bool detectServer);

};

} // namespace charon

#endif // CHARON_UTILS_CLIENT_HPP
