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

#include "client.hpp"

#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"

#include <gloox/message.h>
#include <gloox/messagehandler.h>
#include <gloox/jid.h>

#include <glog/logging.h>

#include <condition_variable>
#include <mutex>

namespace charon
{

namespace
{

/** Default timeout for the client.  */
constexpr auto DEFAULT_TIMEOUT = std::chrono::seconds (3);

/**
 * Abstraction of a started operation that times out after some time.  It also
 * has condition-variable functionality which allows to wait on it (and to
 * signal waiters when done).  Waits automatically take the timeout into account
 * so as to not wait longer than that.
 */
class TimedConditionVariable
{

private:

  /** Clock type used for all measurements.  */
  using Clock = std::chrono::steady_clock;

  /** Time point of Clock when this reaches timeout.  */
  Clock::time_point endTime;

  /** Underlying condition variable.  */
  std::condition_variable cv;

public:

  /**
   * Constructs a new instance, whose end time is the given duration
   * in the future.
   */
  template <typename Rep, typename Period>
    explicit TimedConditionVariable (
        const std::chrono::duration<Rep, Period>& timeout)
    : endTime(Clock::now () + timeout)
  {}

  TimedConditionVariable () = delete;
  TimedConditionVariable (const TimedConditionVariable&) = delete;
  void operator= (const TimedConditionVariable&) = delete;

  /**
   * Waits on the condition variable using the given lock.  Times out
   * at the latest at our endTime.
   */
  void
  Wait (std::unique_lock<std::mutex>& lock)
  {
    if (!IsTimedOut ())
      cv.wait_until (lock, endTime);
  }

  /**
   * Notifies all waiting threads.
   */
  void
  Notify ()
  {
    cv.notify_all ();
  }

  /**
   * Checks whether or not the timeout has been reached.
   */
  bool
  IsTimedOut () const
  {
    return Clock::now () >= endTime;
  }

};

} // anonymous namespace

/**
 * Main implementation logic for the Client class.  This holds all the
 * stuff that is dependent on gloox and other private libraries.
 */
class Client::Impl : public XmppClient,
                     private gloox::MessageHandler
{

private:

  /** Reference to the corresponding Client class.  */
  Client& client;

  /**
   * Mutex used to synchronise all threads, as well as for the various
   * condition variables.
   */
  std::mutex mut;

  /**
   * The selected, full JID of the server we talk to.  This may be equal to
   * client.serverJid and not yet have an associated resource, in which case
   * attempts to send requests will first send a ping and try to set a resource
   * here from the processed pong message.
   */
  gloox::JID fullServerJid;

  /**
   * If there is an on-going ping operation, then this holds a pointer to its
   * condition variable.
   */
  std::weak_ptr<TimedConditionVariable> ongoingPing;

  void handleMessage (const gloox::Message& msg,
                      gloox::MessageSession* session) override;

  /**
   * Returns true if we have a full server JID selected.
   */
  bool
  HasFullServerJid () const
  {
    return !fullServerJid.resource ().empty ();
  }

  /**
   * Tries to ensure that we have a fullServerJid set.  If none is set yet,
   * we send a ping or wait for the completion of an existing ping.
   */
  void TryEnsureFullServerJid (std::unique_lock<std::mutex>& lock);

public:

  explicit Impl (Client& c, const gloox::JID& jid, const std::string& pwd);

  /**
   * Returns the server's resource and tries to find one if none is there.
   */
  std::string GetServerResource ();

};

Client::Impl::Impl (Client& p, const gloox::JID& jid, const std::string& pwd)
  : XmppClient(jid, pwd), client(p), fullServerJid(client.serverJid)
{
  RunWithClient ([this] (gloox::Client& c)
    {
      c.registerStanzaExtension (new RpcRequest ());
      c.registerStanzaExtension (new RpcResponse ());
      c.registerStanzaExtension (new PingMessage ());
      c.registerStanzaExtension (new PongMessage ());

      c.registerMessageHandler (this);
    });
}

void
Client::Impl::TryEnsureFullServerJid (std::unique_lock<std::mutex>& lock)
{
  if (HasFullServerJid ())
    return;

  auto ping = ongoingPing.lock ();
  if (ping == nullptr)
    {
      LOG (INFO) << "No full server JID, sending ping to " << client.serverJid;

      ping = std::make_shared<TimedConditionVariable> (client.timeout);
      RunWithClient ([this] (gloox::Client& c)
        {
          const gloox::JID serverJid(client.serverJid);

          gloox::Message msg(gloox::Message::Normal, serverJid);
          msg.addExtension (new PingMessage ());

          c.send (msg);
        });

      ongoingPing = ping;
    }

  while (true)
    {
      ping->Wait (lock);

      if (ping->IsTimedOut ())
        {
          LOG (WARNING) << "Waiting for pong timed out";
          return;
        }

      if (HasFullServerJid ())
        {
          LOG (INFO) << "We now have a full server JID";
          return;
        }
    }
}

void
Client::Impl::handleMessage (const gloox::Message& msg,
                             gloox::MessageSession* session)
{
  auto* ext = msg.findExtension<PongMessage> (PongMessage::EXT_TYPE);
  if (ext != nullptr)
    {
      std::lock_guard<std::mutex> lock(mut);

      /* In case we get multiple replies, we pick the first only.  */
      if (!HasFullServerJid ())
        {
          fullServerJid = msg.from ();
          LOG (INFO) << "Found full server JID: " << fullServerJid.full ();
        }

      auto ping = ongoingPing.lock ();
      if (ping != nullptr)
        ping->Notify ();
    }
}

std::string
Client::Impl::GetServerResource ()
{
  std::unique_lock<std::mutex> lock(mut);
  TryEnsureFullServerJid (lock);
  return fullServerJid.resource ();
}

Client::Client (const std::string& srv)
  : serverJid(srv)
{
  SetTimeout (DEFAULT_TIMEOUT);
}

Client::~Client () = default;

void
Client::Connect (const std::string& jidStr, const std::string& password,
                 const int priority)
{
  const gloox::JID jid(jidStr);
  impl = std::make_unique<Impl> (*this, jid, password);
  impl->Connect (priority);
}

void
Client::Disconnect ()
{
  if (impl == nullptr)
    return;

  impl->Disconnect ();
  impl.reset ();
}

std::string
Client::GetServerResource ()
{
  CHECK (impl != nullptr);
  return impl->GetServerResource ();
}

} // namespace charon
