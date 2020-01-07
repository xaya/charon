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

#include "testutils.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glog/logging.h>

#include <chrono>
#include <sstream>

using testing::IsEmpty;

namespace charon
{

/* ************************************************************************** */

const char* const XMPP_SERVER = "chat.xaya.io";
const char* const PUBSUB_SERVICE = "pubsub.chat.xaya.io";

/**
 * Our test accounts.  They are all set up for XID on mainnet, and the address
 * CLkoEc3g1XCqF1yevLfE1F2EksLhGSd8GC is set as global signer.  It has the
 * private key LMeJqBHefZdZbH7pHBqhmBu3pFzBpVo78SnBgsoHc6KYaDv9CEYp.
 * The passwords given are unexpiring XID credentials for chat.xaya.io.
 */
const TestAccount ACCOUNTS[] =
  {
    {
      "xmpptest1",
      "CkEfa5+WT2Rc5/TiMDhMynAbSJ+DY9FmE5lcWgWMRQWUBV5UQsgjiBWL302N4kdLZYygJVBV"
      "x3vYsDNUx8xBbw27WA==",
    },
    {
      "xmpptest2",
      "CkEgOEFNwRdLQ6uD543MJLSzip7mTahM1we9GDl3S5NlR49nrJ0JxcFfQmDbbF4C4OpqSlTp"
      "x8OG6xtFjCUMLh/AGA==",
    },
  };

gloox::JID
JIDWithoutResource (const TestAccount& acc)
{
  gloox::JID res;
  res.setUsername (acc.name);
  res.setServer (XMPP_SERVER);
  return res;
}

gloox::JID
JIDWithResource (const TestAccount& acc, const std::string& res)
{
  gloox::JID jid = JIDWithoutResource (acc);
  jid.setResource (res);
  return jid;
}

Json::Value
ParseJson (const std::string& str)
{
  std::istringstream in(str);
  Json::Value res;
  in >> res;
  return res;
}

/* ************************************************************************** */

Json::Value
TestBackend::HandleMethod (const std::string& method, const Json::Value& params)
{
  CHECK (params.isArray ());
  CHECK_EQ (params.size (), 1);
  CHECK (params[0].isString ());

  if (method == "echo")
    return params[0];

  if (method == "error")
    throw Error (42, params[0].asString (), Json::Value ());

  LOG (FATAL) << "Unexpected method: " << method;
}

/* ************************************************************************** */

ReceivedMessages::~ReceivedMessages ()
{
  EXPECT_THAT (messages, IsEmpty ()) << "Unexpected messages received";
}

void
ReceivedMessages::Add (const std::string& msg)
{
  std::lock_guard<std::mutex> lock(mut);
  messages.push_back (msg);
  cv.notify_all ();
}

void
ReceivedMessages::Expect (const std::vector<std::string>& expected)
{
  std::unique_lock<std::mutex> lock(mut);
  while (messages.size () < expected.size ())
    {
      LOG (INFO) << "Waiting for more messages to be received...";
      cv.wait (lock);
    }

  EXPECT_EQ (messages, expected);
  messages.clear ();
}

/* ************************************************************************** */

/**
 * UpdateWaiter implementation for the UpdatableState.
 */
class UpdatableState::Waiter : public UpdateWaiter
{

private:

  /** The underlying state.  */
  UpdatableState& ref;

public:

  explicit Waiter (UpdatableState& s)
    : ref(s)
  {}

  bool
  WaitForUpdate (Json::Value& newState) override
  {
    std::unique_lock<std::mutex> lock(ref.mut);

    ++ref.waitCounter;
    if (ref.waitCounter % 2 == 0)
      return false;

    ref.cv.wait_for (lock, std::chrono::milliseconds (10));

    newState = ref.state;
    return !ref.state.isNull ();
  }

};

Json::Value
UpdatableState::Notification::ExtractStateId (
    const Json::Value& fullState) const
{
  CHECK (fullState.isObject ());
  const auto& id = fullState["id"];
  CHECK (id.isString ());
  return id;
}

Json::Value
UpdatableState::Notification::AlwaysBlockId () const
{
  return "always block";
}

Json::Value
UpdatableState::GetStateJson (const std::string& id, const std::string& value)
{
  Json::Value res(Json::objectValue);
  res["id"] = id;
  res["value"] = value;

  return res;
}

void
UpdatableState::SetState (const std::string& id, const std::string& value)
{
  LOG (INFO) << "Setting new state: " << id << ", " << value;
  std::lock_guard<std::mutex> lock(mut);
  state = GetStateJson (id, value);
  cv.notify_all ();
}

std::unique_ptr<WaiterThread>
UpdatableState::NewWaiter (const std::string& type)
{
  return std::make_unique<WaiterThread> (std::make_unique<Notification> (type),
                                         std::make_unique<Waiter> (*this));
}

/* ************************************************************************** */

} // namespace charon
