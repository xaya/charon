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

#include "client.hpp"

#include "private/pubsub.hpp"
#include "private/stanzas.hpp"
#include "private/xmppclient.hpp"

#include <gloox/error.h>
#include <gloox/iq.h>
#include <gloox/iqhandler.h>
#include <gloox/jid.h>
#include <gloox/message.h>
#include <gloox/presence.h>
#include <gloox/presencehandler.h>

#include <jsonrpccpp/common/errors.h>

#include <glog/logging.h>

#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace charon
{

namespace
{

/* ************************************************************************** */

/** Default timeout for the client.  */
constexpr auto DEFAULT_TIMEOUT = std::chrono::seconds (3);

/** Timeout for waitforchange calls on the client side.  */
constexpr auto WAITFORCHANGE_TIMEOUT = std::chrono::seconds (5);

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

/* ************************************************************************** */

/**
 * Data for an ongoing RPC method call.
 */
struct OngoingRpcCall
{

  /**
   * Possible states for an ongoing RPC call.
   */
  enum class State
  {
    /** The call is waiting for a server response.  */
    WAITING,
    /**
     * The server replied with "service unavailable".  The RPC method
     * corresponding to this request will fail with an internal error.
     *
     * Note that this is something that should rarely happen in practice, since
     * we should have gotten the server's "unavailable" presence notification
     * and reselected a server already when the current one goes away.
     */
    UNAVAILABLE,
    /** We have a response and it was success.  */
    RESPONSE_SUCCESS,
    /** We have a response and it was an error.  */
    RESPONSE_ERROR,
  };

  /** Condition variable (and timeout) for the response.  */
  TimedConditionVariable cv;

  /** Mutex for the condition variable.  */
  std::mutex mut;

  /** The state of this call.  */
  State state;

  /** JID to which we sent.  */
  gloox::JID serverJid;

  /** If success, the RPC result.  */
  Json::Value result;

  /** If error, the thrown error.  */
  RpcServer::Error error;

  template <typename Rep, typename Period>
    explicit OngoingRpcCall (const std::chrono::duration<Rep, Period>& t)
      : cv(t), state(State::WAITING), error(0)
  {}

};

/**
 * IQ handler that waits for a specific RPC method result.
 */
class RpcResultHandler : public gloox::IqHandler
{

private:

  /**
   * Data about the ongoing call.  This will be updated (and the waiting
   * thread notified) when we receive our result.
   */
  std::shared_ptr<OngoingRpcCall> call;

public:

  explicit RpcResultHandler (std::shared_ptr<OngoingRpcCall> c)
    : call(c)
  {}

  RpcResultHandler () = delete;
  RpcResultHandler (const RpcResultHandler&) = delete;
  void operator= (const RpcResultHandler&) = delete;

  bool handleIq (const gloox::IQ& iq) override;
  void handleIqID (const gloox::IQ& iq, int context) override;

};

bool
RpcResultHandler::handleIq (const gloox::IQ& iq)
{
  LOG (WARNING) << "Ignoring IQ without id";
  return false;
}

void
RpcResultHandler::handleIqID (const gloox::IQ& iq, const int context)
{
  std::unique_lock<std::mutex> lock(call->mut);
  if (call->state != OngoingRpcCall::State::WAITING)
    {
      LOG (WARNING) << "Ignoring IQ for non-waiting call";
      return;
    }

  /* If we get a "service unavailable" reply from the server, it means that
     our selected server resource is no longer available.  */
  if (iq.subtype () == gloox::IQ::Error)
    {
      const auto* err = iq.error ();
      if (err != nullptr
            && err->error () == gloox::StanzaErrorServiceUnavailable)
        {
          LOG (WARNING) << "Service unavailable";
          call->state = OngoingRpcCall::State::UNAVAILABLE;
          call->cv.Notify ();
          return;
        }
    }

  if (iq.subtype () != gloox::IQ::Result)
    {
      LOG (WARNING)
          << "Ignoring IQ of type " << iq.subtype ()
          << " from " << iq.from ().full ();
      return;
    }

  const auto* ext = iq.findExtension<RpcResponse> (RpcResponse::EXT_TYPE);
  if (ext == nullptr)
    {
      LOG (WARNING)
          << "Ignoring IQ from " << iq.from ().full ()
          << " without RpcResponse extension";
      return;
    }
  if (!ext->IsValid ())
    {
      LOG (WARNING) << "Ignoring invalid RpcResponse stanza";
      return;
    }

  if (ext->IsSuccess ())
    {
      call->state = OngoingRpcCall::State::RESPONSE_SUCCESS;
      call->result = ext->GetResult ();
    }
  else
    {
      call->state = OngoingRpcCall::State::RESPONSE_ERROR;
      call->error = RpcServer::Error (ext->GetErrorCode (),
                                      ext->GetErrorMessage (),
                                      ext->GetErrorData ());
    }

  call->cv.Notify ();
}

/* ************************************************************************** */

/**
 * The current state for some notification type.  This class keeps track of
 * the known state, updates it when server notifications come in, and also is
 * able to wait for changes (i.e. to implement RPC calls like waitforchange).
 */
class NotificationState
{

private:

  /** NotificationType instance that we use.  */
  std::unique_ptr<NotificationType> notification;

  /** Mutex for this instance.  */
  std::mutex mut;

  /** Condition variable to wait for changes.  */
  std::condition_variable cv;

  /**
   * Whether or not we have any state at all.  This is false initially, and
   * set to true as soon as the "state" variable corresponds to a real state
   * that we received somehow.
   */
  bool hasState = false;

  /** The current state as JSON value.  */
  Json::Value state;

public:

  /**
   * Constructs a new instance for the given notification type.
   */
  explicit NotificationState (std::unique_ptr<NotificationType> n)
    : notification(std::move (n))
  {}

  NotificationState () = delete;
  NotificationState (const NotificationState&) = delete;
  void operator= (const NotificationState&) = delete;

  /**
   * Waits (up to our predefined timeout) until the state changes.  Returns
   * immediately if the current state does not match the given known ID.
   */
  Json::Value WaitForChange (const Json::Value& known);

  /**
   * Returns a pubsub ItemCallback that will set our state to the passed in
   * new state and notify waiters.
   */
  PubSubImpl::ItemCallback GetItemCallback ();

};

Json::Value
NotificationState::WaitForChange (const Json::Value& known)
{
  std::unique_lock<std::mutex> lock(mut);

  if (hasState && known != notification->AlwaysBlockId ())
    {
      const auto stateId = notification->ExtractStateId (state);
      if (known != stateId)
        {
          VLOG (1)
              << "Current state ID " << stateId
              << " does not match known " << known;
          return state;
        }
    }

  VLOG (1) << "Starting wait for " << notification->GetType () << "...";

  cv.wait_for (lock, WAITFORCHANGE_TIMEOUT);
  return state;
}

PubSubImpl::ItemCallback
NotificationState::GetItemCallback ()
{
  return [this] (const gloox::Tag& t)
    {
      const auto& type = notification->GetType ();

      VLOG (1)
          << "Processing update notification for " << type << ":\n" << t.xml ();

      const auto* updTag = t.findChild ("update");
      if (updTag == nullptr)
        {
          LOG (WARNING)
              << "Ignoring update without our payload:\n" << t.xml ();
          return;
        }

      const NotificationUpdate upd(*updTag);
      if (!upd.IsValid ())
        {
          LOG (WARNING)
              << "Ignoring invalid payload update:\n" << t.xml ();
          return;
        }

      if (upd.GetType () != type)
        {
          LOG (WARNING)
              << "Ignoring update for different type (got " << upd.GetType ()
              << ", waiting for " << type << "):\n"
              << t.xml ();
          return;
        }

      std::lock_guard<std::mutex> lock(mut);
      hasState = true;
      state = upd.GetState ();

      LOG (INFO) << "Found new state for " << type;
      VLOG (1) << "New state:\n" << state;

      cv.notify_all ();
    };
}

} // anonymous namespace

/* ************************************************************************** */

/**
 * Main implementation logic for the Client class.  This holds all the
 * stuff that is dependent on gloox and other private libraries.
 */
class Client::Impl : public XmppClient,
                     private gloox::PresenceHandler
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
   * Threads that are currently running pubsub subscriptions or have run some
   * in the past.  We mostly just collect threads here that will finish by
   * themselves and be joined in the destructor, although we also explicitly
   * join them in some situations (e.g. when GetServerResource is called
   * explicitly to force server selection).
   */
  std::vector<std::thread> subscribeCalls;

  /**
   * If there is an on-going ping operation, then this holds a pointer to its
   * condition variable.
   */
  std::weak_ptr<TimedConditionVariable> ongoingPing;

  /** Current states for all the enabled notifications.  */
  std::map<std::string, std::unique_ptr<NotificationState>> states;

  void handlePresence (const gloox::Presence& p) override;

  /**
   * Returns true if we have a full server JID selected.
   */
  bool
  HasFullServerJid () const
  {
    return !fullServerJid.resource ().empty ();
  }

  /**
   * Clears our selected server.  This is done when either the server
   * goes offline (we receive an unavailable presence for it), or if the
   * XMPP connection itself is closed.
   */
  void ClearSelectedServer ();

  /**
   * Sets the selected server to the given full JID.  This also notifies
   * all waiters on that.
   */
  void SetSelectedServer (std::unique_lock<std::mutex>& lock,
                          const gloox::JID& jid,
                          const SupportedNotifications* sn);

  /**
   * Tries to ensure that we have an active XMPP connection and also a
   * fullServerJid set.  If none is set yet, we send a ping or wait for the
   * completion of an existing ping.
   *
   * Returns the server JID to send requests to, or an invalid JID if we
   * could not detect a server or connect.
   */
  gloox::JID EnsureConnected ();

  /**
   * Forces all ongoing node subscriptions to be finished.  The caller is
   * supposed to pass in a lock on mut, which will be released while
   * waiting for the subscription threads (to avoid deadlocks).
   */
  void FinishSubscriptions (std::unique_lock<std::mutex>& lock);

protected:

  void HandleDisconnect () override;

public:

  explicit Impl (Client& c, const gloox::JID& jid, const std::string& pwd);

  ~Impl ();

  /**
   * Enables a new notification.
   */
  void AddNotification (std::unique_ptr<NotificationType> n);

  /**
   * Returns the server's resource and tries to find one if none is there.
   */
  std::string GetServerResource ();

  /**
   * Forwards the given RPC call to the server.
   */
  Json::Value ForwardMethod (const std::string& method,
                             const Json::Value& params);

  /**
   * Waits for a state change of the given notification type.
   */
  Json::Value WaitForChange (const std::string& type, const Json::Value& known);

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
      c.registerStanzaExtension (new SupportedNotifications ());

      c.registerPresenceHandler (this);
    });
}

Client::Impl::~Impl ()
{
  RunWithClient ([this] (gloox::Client& c)
    {
      c.removePresenceHandler (this);
    });

  std::unique_lock<std::mutex> lock(mut);
  FinishSubscriptions (lock);
}

void
Client::Impl::AddNotification (std::unique_ptr<NotificationType> n)
{
  const auto& type = n->GetType ();
  auto s = std::make_unique<NotificationState> (std::move (n));
  const auto res = states.emplace (type, std::move (s));
  CHECK (res.second) << "Duplicate notification of type " << type;
}

gloox::JID
Client::Impl::EnsureConnected ()
{
  std::unique_lock<std::mutex> lock(mut);

  if (HasFullServerJid ())
    return fullServerJid;

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
          return gloox::JID ();
        }

      if (HasFullServerJid ())
        {
          LOG (INFO) << "We now have a full server JID";
          return fullServerJid;
        }
    }
}

void
Client::Impl::ClearSelectedServer ()
{
  fullServerJid = client.serverJid;
}

void
Client::Impl::SetSelectedServer (std::unique_lock<std::mutex>& lock,
                                 const gloox::JID& jid,
                                 const SupportedNotifications* sn)
{
  CHECK_EQ (jid.bareJID (), fullServerJid.bareJID ());

  fullServerJid = jid;
  LOG (INFO)
      << "Found full server JID: " << fullServerJid.full ();

  gloox::Presence resp(gloox::Presence::Available, jid);
  RunWithClient ([&resp] (gloox::Client& c)
    {
      c.send (resp);
    });

  /* By adding the pubsub instance here to our client, we also
     replace any existing one and make sure that it is connected
     to the service indicated by the server.  */
  if (!states.empty ())
    {
      CHECK (sn != nullptr);

      /* Before recreating the pubsub instance, we have to make sure
         that all running calls to it are done to avoid any memory
         corruption and race conditions.  */
      FinishSubscriptions (lock);

      AddPubSub (sn->GetService ());

      const auto& n = sn->GetNotifications ();
      for (auto& entry : states)
        {
          const auto mit = n.find (entry.first);
          CHECK (mit != n.end ());

          const std::string node = mit->second;
          auto cb = entry.second->GetItemCallback ();

          LOG (INFO)
              << "Subscribing to node " << node
              << " for notification " << entry.first;

          /* The call to SubscribeToNode waits for the subscription
             response from the server, so we have to do it async.  */
          subscribeCalls.emplace_back ([this, node, cb] ()
            {
              GetPubSub ().SubscribeToNode (node, cb);
            });
        }
    }
}

void
Client::Impl::handlePresence (const gloox::Presence& p)
{
  switch (p.subtype ())
    {
    case gloox::Presence::Available:
      {
        const auto* pong = p.findExtension<PongMessage> (PongMessage::EXT_TYPE);
        if (pong == nullptr || !pong->IsValid ())
          return;
        if (pong->GetVersion () != client.version)
          {
            LOG (WARNING)
                << "Server " << p.from ().full ()
                << " reported version " << pong->GetVersion ()
                << " while we want " << client.version;
            return;
          }

        const auto* sn = p.findExtension<SupportedNotifications> (
            SupportedNotifications::EXT_TYPE);
        for (const auto& entry : states)
          {
            if (sn == nullptr)
              {
                LOG (WARNING)
                    << "Server " << p.from ().full ()
                    << " does not support notifications, ignoring";
                return;
              }

            const auto& n = sn->GetNotifications ();
            if (n.find (entry.first) == n.end ())
              {
                LOG (WARNING)
                    << "Server " << p.from ().full ()
                    << " does not support notification " << entry.first;
                return;
              }
          }

        std::unique_lock<std::mutex> lock(mut);

        if (p.from ().bareJID () != fullServerJid.bareJID ())
          {
            LOG (WARNING)
                << "Ignoring pong not from our server but from "
                << p.from ().full ();
            return;
          }

        /* In case we get multiple replies, we pick the first only.  */
        if (!HasFullServerJid ())
          SetSelectedServer (lock, p.from (), sn);

        auto ping = ongoingPing.lock ();
        if (ping != nullptr)
          ping->Notify ();

        return;
      }

    case gloox::Presence::Unavailable:
      {
        std::lock_guard<std::mutex> lock(mut);
        if (p.from () == fullServerJid)
          {
            LOG (WARNING) << "Our server has become unavailable";
            ClearSelectedServer ();
          }
        return;
      }

    default:
      return;
    }
}

void
Client::Impl::HandleDisconnect ()
{
  std::lock_guard<std::mutex> lock(mut);
  ClearSelectedServer ();
}

std::string
Client::Impl::GetServerResource ()
{
  const auto jid = EnsureConnected ();

  std::unique_lock<std::mutex> lock(mut);
  FinishSubscriptions (lock);

  return jid.resource ();
}

void
Client::Impl::FinishSubscriptions (std::unique_lock<std::mutex>& lock)
{
  for (auto& t : subscribeCalls)
    {
      /* Make sure to unlock temporarily while joining the thread.  Otherwise
         we might be preventing the XMPP thread to process certain
         incoming messages (e.g. presences) that need a lock, which would
         mean that the subscription may never be finished and we deadlock.  */
      lock.unlock ();
      t.join ();
      lock.lock ();
    }
  subscribeCalls.clear ();
}

Json::Value
Client::Impl::ForwardMethod (const std::string& method,
                             const Json::Value& params)
{
  const auto jid = EnsureConnected ();
  if (!jid)
    {
      std::ostringstream msg;
      msg << "could not discover full server JID for " << client.serverJid;
      throw RpcServer::Error (jsonrpc::Errors::ERROR_RPC_INTERNAL_ERROR,
                              msg.str ());
    }

  auto iq = std::make_unique<gloox::IQ> (gloox::IQ::Get, jid);
  iq->addExtension (new RpcRequest (method, params));

  auto call = std::make_shared<OngoingRpcCall> (client.timeout);
  call->serverJid = iq->to ();
  RunWithClient ([&] (gloox::Client& c)
    {
      LOG (INFO)
          << "Sending IQ request for method " << method
          << " to " << iq->to ().full ();
      c.send (*iq, new RpcResultHandler (call), 0, true);
    });
  iq.reset ();

  while (true)
    {
      std::unique_lock<std::mutex> callLock(call->mut);
      call->cv.Wait (callLock);

      switch (call->state)
        {
        case OngoingRpcCall::State::RESPONSE_SUCCESS:
          LOG (INFO) << "Received success call result";
          return call->result;
        case OngoingRpcCall::State::RESPONSE_ERROR:
          LOG (INFO) << "Received error call result";
          throw call->error;

        case OngoingRpcCall::State::UNAVAILABLE:
          throw RpcServer::Error (jsonrpc::Errors::ERROR_RPC_INTERNAL_ERROR,
                                  "selected server is unavailable");

        default:
          break;
        }

      if (call->cv.IsTimedOut ())
        {
          LOG (WARNING) << "Call to " << method << " timed out";
          std::ostringstream msg;
          msg << "timeout waiting for result from " << call->serverJid.full ();
          throw RpcServer::Error (jsonrpc::Errors::ERROR_RPC_INTERNAL_ERROR,
                                  msg.str ());
        }
    }
}

Json::Value
Client::Impl::WaitForChange (const std::string& type, const Json::Value& known)
{
  const auto jid = EnsureConnected ();
  if (!jid)
    {
      std::ostringstream msg;
      msg << "could not discover full server JID for " << client.serverJid;
      throw RpcServer::Error (jsonrpc::Errors::ERROR_RPC_INTERNAL_ERROR,
                              msg.str ());
    }

  const auto mit = states.find (type);
  CHECK (mit != states.end ()) << "Notification type not enabled: " << type;
  return mit->second->WaitForChange (known);
}

/* ************************************************************************** */

Client::Client (const std::string& srv, const std::string& v,
                const std::string& jidStr, const std::string& password)
  : serverJid(srv), version(v)
{
  SetTimeout (DEFAULT_TIMEOUT);

  const gloox::JID jid(jidStr);
  impl = std::make_unique<Impl> (*this, jid, password);
}

Client::~Client () = default;

void
Client::Connect ()
{
  CHECK (impl != nullptr);

  /* We always connect with priority -1, because the client will never want
     to receive any messages for the bare JID.  It only communicates with
     its full JID.  */
  impl->Connect (-1);
}

void
Client::Disconnect ()
{
  CHECK (impl != nullptr);
  impl->Disconnect ();
}

void
Client::AddNotification (std::unique_ptr<NotificationType> n)
{
  CHECK (impl != nullptr);
  impl->AddNotification (std::move (n));
}

std::string
Client::GetServerResource ()
{
  CHECK (impl != nullptr);
  return impl->GetServerResource ();
}

Json::Value
Client::ForwardMethod (const std::string& method, const Json::Value& params)
{
  CHECK (impl != nullptr);
  return impl->ForwardMethod (method, params);
}

Json::Value
Client::WaitForChange (const std::string& type, const Json::Value& known)
{
  CHECK (impl != nullptr);
  return impl->WaitForChange (type, known);
}

/* ************************************************************************** */

} // namespace charon
