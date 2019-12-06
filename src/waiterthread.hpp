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

#ifndef CHARON_WAITERTHREAD_HPP
#define CHARON_WAITERTHREAD_HPP

#include "notifications.hpp"

#include <json/json.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace charon
{

/**
 * Interface for a method to wait for updates to some state in the backend GSP.
 * This can simply be an RPC call to e.g. waitforchange, but it can also be
 * implemented e.g. as a direct method call on a libxayagame Game object.
 */
class UpdateWaiter
{

public:

  UpdateWaiter () = default;
  virtual ~UpdateWaiter () = default;

  /**
   * Subclasses must implement this method to wait for an update of the state.
   * The method should return true if the call was successful and we may have
   * a new state in the output argument.  If it returns false, it means that
   * the call may be retried, e.g. because of a timeout error.
   */
  virtual bool WaitForUpdate (Json::Value& newState) = 0;

};

/**
 * A thread that waits for updates in the GSP using a "long-polling"
 * UpdateWaiter instance.  It just keeps calling in a loop, and keeps its own
 * internal record of the current state.
 */
class WaiterThread
{

public:

  /** Type of callback for state changes.  */
  using UpdateHandler = std::function<void (const Json::Value& newState)>;

private:

  /** NotificationType that this is for.  */
  std::unique_ptr<NotificationType> type;

  /** UpdateWaiter we use.  */
  std::unique_ptr<UpdateWaiter> waiter;

  /** The running loop thread if any.  */
  std::unique_ptr<std::thread> loop;

  /**
   * Mutex for the state that is accessed from multiple threads (like the
   * currentState value).
   */
  mutable std::mutex mut;

  /** Set to true if the loop should stop.  */
  std::atomic<bool> shouldStop;

  /**
   * Current state from the polling loop.  May be JSON null when we have
   * just started the loop and not yet received an update.
   */
  Json::Value currentState;

  /** Callback to be invoked whenever the state changes.  */
  UpdateHandler cb;

  /**
   * Performs the waiting loop.  This is what the loop thread executes.
   */
  void RunLoop ();

public:

  /**
   * Constructs the thread, which is not yet running.
   */
  explicit WaiterThread (std::unique_ptr<NotificationType> t,
                         std::unique_ptr<UpdateWaiter> w)
    : type(std::move (t)), waiter(std::move (w))
  {}

  WaiterThread () = delete;
  WaiterThread (const WaiterThread&) = delete;
  void operator= (const WaiterThread&) = delete;

  /**
   * The destructor verifies that the loop is no longer running.  Before
   * the object is destroyed, the running loop must be stopped explicitly.
   */
  virtual ~WaiterThread ();

  /**
   * Starts the waiter thread loop.
   */
  void Start ();

  /**
   * Stops the threading loop.
   */
  void Stop ();

  /**
   * Returns the notification type name.
   */
  const std::string&
  GetType () const
  {
    return type->GetType ();
  }

  /**
   * Returns the current state in a non-blocking way.
   */
  Json::Value GetCurrentState () const;

  /**
   * Sets / replaces the handler for updates.
   */
  void SetUpdateHandler (const UpdateHandler& h);

};

} // namespace charon

#endif // CHARON_WAITERTHREAD_HPP
