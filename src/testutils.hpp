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

#ifndef CHARON_TESTUTILS_HPP
#define CHARON_TESTUTILS_HPP

#include "rpcserver.hpp"
#include "waiterthread.hpp"

#include <gloox/jid.h>

#include <json/json.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace charon
{

/** XMPP server used for testing.  */
extern const char* const XMPP_SERVER;

/** Pubsub service at the server.  */
extern const char* const PUBSUB_SERVICE;

/**
 * Data for one of the test accounts that we use.
 */
struct TestAccount
{

  /** The username for the XMPP server.  */
  const char* name;

  /** The password for logging into the server.  */
  const char* password;

};

/** Test accounts on the server.  */
extern const TestAccount ACCOUNTS[2];

/**
 * Constructs the JID for a test account, without resource.
 */
gloox::JID JIDWithoutResource (const TestAccount& acc);

/**
 * Constructs the JID for a test account with the given resource.
 */
gloox::JID JIDWithResource (const TestAccount& acc, const std::string& res);

/**
 * Parses a string as JSON (for use in test data).
 */
Json::Value ParseJson (const std::string& str);

/**
 * Backend for answering RPC calls in a dummy fashion.  It supports two
 * methods (both accept a single string as positional argument):  "echo"
 * returns the argument back to the caller, while "error" throws a JSON-RPC
 * error with the string as message.
 */
class TestBackend : public RpcServer
{

public:

  TestBackend () = default;

  Json::Value HandleMethod (const std::string& method,
                            const Json::Value& params) override;

};

/**
 * A synchronised queue for received vs expected messages.  This can be used
 * to add messages from some handler thread, and expect to receive a given
 * set of messages from the test itself.
 */
class ReceivedMessages
{

private:

  /** The messages themselves, as received in order.  */
  std::vector<std::string> messages;

  /**
   * Mutex for synchronising the receiving messages and waiting for them
   * when comparing to expectations.
   */
  std::mutex mut;

  /** Condition variable for waiting for more messages.  */
  std::condition_variable cv;

public:

  ReceivedMessages () = default;
  ~ReceivedMessages ();

  /**
   * Adds a newly received message to the queue and potentially signals
   * waiting threads.
   */
  void Add (const std::string& msg);

  /**
   * Expects to receive the given messages in order.  Waits for them to
   * arrive as needed, and clears out the message queue at the end.
   */
  void Expect (const std::vector<std::string>& expected);

};

/**
 * Fake implementation of an external "game state", that can be updated on
 * demand and which can serve as "backend" for update notifications in the
 * server (e.g. as waitforchange source for the backend GSP).
 *
 * For tests, we use state in the JSON form {"id":"foo", "value":"bar"}, where
 * "foo" is the identifier of the state, and "bar" some other value to make
 * it comparable and distinguishable as needed in tests.
 */
class UpdatableState
{

private:

  class Waiter;

  /** The current state JSON.  */
  Json::Value state;

  /** Mutex for the instance and waiters.  */
  std::mutex mut;

  /** Condition variable for the waiting threads.  */
  std::condition_variable cv;

  /**
   * Counter for calls to WaitForChange.  This is used to make the call fail
   * every second time, so we can verify this is tolerated.
   */
  unsigned waitCounter = 0;

public:

  class Notification;

  UpdatableState () = default;

  UpdatableState (const UpdatableState&) = delete;
  void operator= (const UpdatableState&) = delete;

  /**
   * Constructs a new WaiterThread instance with the given type string
   * and waiting for updates on this state.
   */
  std::unique_ptr<WaiterThread> NewWaiter (const std::string& type);

  /**
   * Updates the current state.
   */
  void SetState (const std::string& id, const std::string& value);

  /**
   * Constructs the state JSON in our test format for a given ID and value.
   */
  static Json::Value GetStateJson (const std::string& id,
                                   const std::string& value);

};

/**
 * Test notification type for our UpdatableState.
 */
class UpdatableState::Notification : public NotificationType
{

public:

  explicit Notification (const std::string& type)
    : NotificationType (type)
  {}

  Json::Value ExtractStateId (const Json::Value& fullState) const override;
  Json::Value AlwaysBlockId () const override;

};

} // namespace charon

#endif // CHARON_TESTUTILS_HPP
