/*
    Charon - a transport system for GSP data
    Copyright (C) 2019-2021  Autonomous Worlds Ltd

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

#include <experimental/filesystem>

#include <chrono>
#include <cstdlib>
#include <sstream>

using testing::IsEmpty;

namespace charon
{

namespace fs = std::experimental::filesystem;

/* ************************************************************************** */

namespace
{

/**
 * Configuration for the local test environment.
 */
const ServerConfiguration LOCAL_SERVER =
  {
    "localhost",
    "pubsub.localhost",
    "testenv.pem",
    {
      {"xmpptest1", "password"},
      {"xmpptest2", "password"},
    },
  };

/**
 * Configuration on the production server (chat.xaya.io).  The accounts are all
 * set up for XID on mainnet, and the address
 * CLkoEc3g1XCqF1yevLfE1F2EksLhGSd8GC is set as global signer.  It has the
 * private key LMeJqBHefZdZbH7pHBqhmBu3pFzBpVo78SnBgsoHc6KYaDv9CEYp.
 * The passwords given are unexpiring XID credentials for chat.xaya.io.
 */
const ServerConfiguration PROD_SERVER =
  {
    "chat.xaya.io",
    "pubsub.chat.xaya.io",
    "letsencrypt.pem",
    {
      {
        "xmpptest1",
        "CkEfa5+WT2Rc5/TiMDhMynAbSJ+DY9FmE5lcWgWMRQWUBV5UQsgjiBWL302N4kdLZYygJV"
        "BVx3vYsDNUx8xBbw27WA==",
      },
      {
        "xmpptest2",
        "CkEgOEFNwRdLQ6uD543MJLSzip7mTahM1we9GDl3S5NlR49nrJ0JxcFfQmDbbF4C4OpqSl"
        "Tpx8OG6xtFjCUMLh/AGA==",
      },
    },
  };

} // anonymous namespace

const ServerConfiguration&
GetServerConfig ()
{
  const char* srvPtr = std::getenv ("CHARON_TEST_SERVER");
  std::string srv;
  if (srvPtr == nullptr)
    srv = "localhost";
  else
    srv = srvPtr;

  LOG_FIRST_N (INFO, 1) << "Using test server: " << srv;

  if (srv == "localhost")
    return LOCAL_SERVER;
  if (srv == "chat.xaya.io")
    return PROD_SERVER;

  LOG (FATAL) << "Invalid test server chosen: " << srv;
}

const TestAccount&
GetTestAccount (const unsigned n)
{
  return GetServerConfig ().accounts[n];
}

std::string
GetTestCA ()
{
  const char* top = std::getenv ("top_srcdir");
  if (top == nullptr)
    top = "..";

  return fs::path (top) / "data" / GetServerConfig ().cafile;
}

/* ************************************************************************** */

gloox::JID
JIDWithoutResource (const TestAccount& acc)
{
  gloox::JID res;
  res.setUsername (acc.name);
  res.setServer (GetServerConfig ().server);
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
  std::shared_ptr<UpdatableState> ref;

public:

  explicit Waiter (std::shared_ptr<UpdatableState> s)
    : ref(std::move (s))
  {}

  bool
  WaitForUpdate (Json::Value& newState) override
  {
    std::unique_lock<std::mutex> lock(ref->mut);
    ++ref->calls;

    if (ref->fail)
      return false;

    ref->cv.wait_for (lock, std::chrono::milliseconds (10));

    newState = ref->state;
    return true;
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

UpdatableState::Handle
UpdatableState::Create ()
{
  Handle res(new UpdatableState ());
  res->self = res;
  return res;
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

void
UpdatableState::SetShouldFail (const bool val)
{
  LOG (INFO) << "Setting should fail for UpdatableState to: " << val;
  std::lock_guard<std::mutex> lock(mut);
  fail = val;
  cv.notify_all ();
}

unsigned
UpdatableState::GetNumCalls () const
{
  std::lock_guard<std::mutex> lock(mut);
  return calls;
}

std::unique_ptr<WaiterThread>
UpdatableState::NewWaiter (const std::string& type)
{
  auto notification = std::make_unique<Notification> (type);
  CHECK (!self.expired ());
  auto waiter = std::make_unique<Waiter> (self.lock ());
  return std::make_unique<WaiterThread> (std::move (notification),
                                         std::move (waiter));
}

/* ************************************************************************** */

} // namespace charon
