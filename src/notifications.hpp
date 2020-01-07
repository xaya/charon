/*
    Charon - a transport system for GSP data
    Copyright (C) 2020  Autonomous Worlds Ltd

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

#ifndef CHARON_NOTIFICATIONS_HPP
#define CHARON_NOTIFICATIONS_HPP

#include <json/json.h>

#include <string>

namespace charon
{

/**
 * Interface that defines the specifics of a particular notification type
 * that we can support.  This handles the interface of the notification,
 * i.e. what JSON data it contains, how states are "identified" (e.g. the
 * associated block hash or a pending version ID), and how a client can
 * retrieve the initial state from a Charon server on startup.
 *
 * This does not specify the particular way in which a Charon server will
 * talk to its GSP backend for getting updates itself, because that is an
 * orthogonal issue and may depend on the concrete GSP structure.
 */
class NotificationType
{

private:

  /** The type of this notification as string.  */
  const std::string type;

protected:

  /**
   * Constructs the instance.  Subclasses must define the type string.
   */
  explicit NotificationType (const std::string& t)
    : type(t)
  {}

public:

  NotificationType () = delete;
  NotificationType (const NotificationType&) = delete;
  void operator= (const NotificationType&) = delete;

  virtual ~NotificationType () = default;

  /**
   * Returns the type string.
   */
  const std::string&
  GetType () const
  {
    return type;
  }

  /**
   * Subclasses must implement this method to return the parameter value
   * for "current state" that uniquely identifies its "version".  This value
   * must be usable to compare for equality with a known state, and is also
   * what gets passed as "known version" argument to the waiter method.
   *
   * The method should just be a pure function of its input.
   */
  virtual Json::Value ExtractStateId (const Json::Value& fullState) const = 0;

  /**
   * Should return the state ID value that indicates that WaitForChange should
   * always block (i.e. that there is no currently known state).
   */
  virtual Json::Value AlwaysBlockId () const = 0;

};

/**
 * NotificationType implementation for the game-state update with new blocks,
 * i.e. matching the waitforchange RPC method.
 */
class StateChangeNotification : public NotificationType
{

public:

  StateChangeNotification ()
    : NotificationType ("state")
  {}

  Json::Value ExtractStateId (const Json::Value& fullState) const override;
  Json::Value AlwaysBlockId () const override;

};

/**
 * NotificationType implementation for the pending-move update, matching
 * the waitforpendingchange polling RPC.
 */
class PendingChangeNotification : public NotificationType
{

public:

  PendingChangeNotification ()
    : NotificationType ("pending")
  {}

  Json::Value ExtractStateId (const Json::Value& fullState) const override;
  Json::Value AlwaysBlockId () const override;

};

} // namespace charon

#endif // CHARON_NOTIFICATIONS_HPP
