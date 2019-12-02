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

#include <gloox/error.h>
#include <gloox/iq.h>
#include <gloox/iqhandler.h>
#include <gloox/message.h>
#include <gloox/messagehandler.h>
#include <gloox/jid.h>

#include <jsonrpccpp/common/errors.h>

#include <glog/logging.h>

#include <condition_variable>
#include <mutex>
#include <sstream>

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
     * The server replied with "service unavailable" and we should try
     * to select another resource.
     */
    RESELECT,
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
     our selected server resource is no longer available and we should reselect
     another one.  */
  if (iq.subtype () == gloox::IQ::Error)
    {
      const auto* err = iq.error ();
      if (err != nullptr
            && err->error () == gloox::StanzaErrorServiceUnavailable)
        {
          LOG (WARNING)
              << "Service unavailable, we need to reselect the server JID";
          call->state = OngoingRpcCall::State::RESELECT;
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

  /**
   * Forwards the given RPC call to the server.
   */
  Json::Value ForwardMethod (const std::string& method,
                             const Json::Value& params);

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

Json::Value
Client::Impl::ForwardMethod (const std::string& method,
                             const Json::Value& params)
{
  std::unique_ptr<gloox::IQ> iq;
  {
    std::unique_lock<std::mutex> lock(mut);
    TryEnsureFullServerJid (lock);

    if (!HasFullServerJid ())
      {
        std::ostringstream msg;
        msg << "could not discover full server JID for " << client.serverJid;
        throw RpcServer::Error (jsonrpc::Errors::ERROR_RPC_INTERNAL_ERROR,
                                msg.str ());
      }

    iq = std::make_unique<gloox::IQ> (gloox::IQ::Get, fullServerJid);
    iq->addExtension (new RpcRequest (method, params));
  }

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

        case OngoingRpcCall::State::RESELECT:
          {
            std::unique_lock<std::mutex> lock(mut);
            if (fullServerJid == call->serverJid)
              fullServerJid = fullServerJid.bareJID ();
          }
          return ForwardMethod (method, params);

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

Json::Value
Client::ForwardMethod (const std::string& method, const Json::Value& params)
{
  CHECK (impl != nullptr);
  return impl->ForwardMethod (method, params);
}

} // namespace charon
