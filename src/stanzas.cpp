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

#include "private/stanzas.hpp"

#include "xmldata.hpp"

#include <glog/logging.h>

#include <sstream>

namespace charon
{

/** XML namespace for our stanza extensions.  */
#define XMLNS "https://xaya.io/charon/"

/* ************************************************************************** */

RpcRequest::RpcRequest ()
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (false);
}

RpcRequest::RpcRequest (const std::string& m, const Json::Value& p)
  : ValidatedStanzaExtension(EXT_TYPE),
    method(m), params(p)
{
  SetValid (true);
}

RpcRequest::RpcRequest (const gloox::Tag& t)
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (false);

  const auto* child = t.findChild ("method");
  if (child == nullptr)
    {
      LOG (WARNING) << "request tag has no method child";
      return;
    }
  method = child->cdata ();
  if (method == "")
    {
      LOG (WARNING) << "request tag has empty method";
      return;
    }

  child = t.findChild ("params");
  if (child == nullptr)
    {
      LOG (WARNING) << "request tag has no params child";
      return;
    }
  if (!DecodeXmlJson (*child, params))
    return;
  if (!params.isObject () && !params.isArray () && !params.isNull ())
    {
      LOG (WARNING) << "request params is neither object nor array";
      return;
    }

  SetValid (true);
}

const std::string&
RpcRequest::filterString () const
{
  static const std::string filter = "/*/request[@xmlns='" XMLNS "']";
  return filter;
}

gloox::StanzaExtension*
RpcRequest::newInstance (const gloox::Tag* tag) const
{
  return new RpcRequest (*tag);
}

gloox::StanzaExtension*
RpcRequest::clone () const
{
  auto res = std::make_unique<RpcRequest> ();

  if (IsValid ())
    {
      res->method = method;
      res->params = params;
      res->SetValid (true);
    }
  else
    res->SetValid (false);

  return res.release ();
}

gloox::Tag*
RpcRequest::tag () const
{
  CHECK (IsValid ()) << "Trying to serialise invalid RpcRequest";

  auto res = std::make_unique<gloox::Tag> ("request");
  CHECK (res->setXmlns (XMLNS));

  auto child = std::make_unique<gloox::Tag> ("method", method);
  res->addChild (child.release ());

  child = EncodeXmlJson ("params", params);
  res->addChild (child.release ());

  return res.release ();
}

/* ************************************************************************** */

RpcResponse::RpcResponse ()
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (false);
}

RpcResponse::RpcResponse (const Json::Value& res)
  : ValidatedStanzaExtension(EXT_TYPE),
    success(true), result(res)
{
  SetValid (true);
}

RpcResponse::RpcResponse (const int c, const std::string& msg,
                          const Json::Value& d)
  : ValidatedStanzaExtension(EXT_TYPE),
    success(false), errorCode(c), errorMsg(msg), errorData(d)
{
  SetValid (true);
}

RpcResponse::RpcResponse (const gloox::Tag& t)
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (false);

  const auto* outer = t.findChild ("result");
  if (outer != nullptr)
    {
      if (t.hasChild ("error"))
        {
          LOG (WARNING) << "response tag has both result and error childs";
          return;
        }

      if (!DecodeXmlJson (*outer, result))
        return;

      success = true;
      SetValid (true);
      return;
    }

  outer = t.findChild ("error");
  if (outer == nullptr)
    {
      LOG (WARNING) << "response tag has neither result nor error";
      return;
    }

  if (!outer->hasAttribute ("code"))
    {
      LOG (WARNING) << "error element has no code attribute";
      return;
    }
  std::istringstream codeIn(outer->findAttribute ("code"));
  codeIn >> errorCode;

  const auto* child = outer->findChild ("message");
  if (child == nullptr)
    errorMsg = "";
  else
    errorMsg = child->cdata ();

  child = outer->findChild ("data");
  if (child == nullptr)
    errorData = Json::Value ();
  else if (!DecodeXmlJson (*child, errorData))
    return;

  success = false;
  SetValid (true);
}

const Json::Value&
RpcResponse::GetResult () const
{
  CHECK (IsSuccess ());
  return result;
}

int
RpcResponse::GetErrorCode () const
{
  CHECK (!IsSuccess ());
  return errorCode;
}

const std::string&
RpcResponse::GetErrorMessage () const
{
  CHECK (!IsSuccess ());
  return errorMsg;
}

const Json::Value&
RpcResponse::GetErrorData () const
{
  CHECK (!IsSuccess ());
  return errorData;
}

const std::string&
RpcResponse::filterString () const
{
  static const std::string filter = "/*/response[@xmlns='" XMLNS "']";
  return filter;
}

gloox::StanzaExtension*
RpcResponse::newInstance (const gloox::Tag* tag) const
{
  return new RpcResponse (*tag);
}

gloox::StanzaExtension*
RpcResponse::clone () const
{
  auto res = std::make_unique<RpcResponse> ();

  if (IsValid ())
    {
      res->success = success;
      res->result = result;
      res->errorCode = errorCode;
      res->errorMsg = errorMsg;
      res->errorData = errorData;
      res->SetValid (true);
    }
  else
    res->SetValid (false);

  return res.release ();
}

gloox::Tag*
RpcResponse::tag () const
{
  CHECK (IsValid ()) << "Trying to serialise invalid RpcResponse";

  auto res = std::make_unique<gloox::Tag> ("response");
  CHECK (res->setXmlns (XMLNS));

  if (success)
    {
      auto child = EncodeXmlJson ("result", result);
      res->addChild (child.release ());
    }
  else
    {
      auto outer = std::make_unique<gloox::Tag> ("error");
      CHECK (outer->addAttribute ("code", errorCode));

      if (!errorMsg.empty ())
        {
          auto child = std::make_unique<gloox::Tag> ("message", errorMsg);
          outer->addChild (child.release ());
        }

      if (!errorData.isNull ())
        {
          auto child = EncodeXmlJson ("data", errorData);
          outer->addChild (child.release ());
        }

      res->addChild (outer.release ());
    }

  return res.release ();
}

/* ************************************************************************** */

PingMessage::PingMessage ()
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (true);
}

const std::string&
PingMessage::filterString () const
{
  static const std::string filter = "/*/ping[@xmlns='" XMLNS "']";
  return filter;
}

gloox::StanzaExtension*
PingMessage::newInstance (const gloox::Tag* tag) const
{
  return new PingMessage ();
}

gloox::StanzaExtension*
PingMessage::clone () const
{
  return new PingMessage ();
}

gloox::Tag*
PingMessage::tag () const
{
  auto res = std::make_unique<gloox::Tag> ("ping");
  CHECK (res->setXmlns (XMLNS));

  return res.release ();
}

PongMessage::PongMessage ()
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (false);
}

PongMessage::PongMessage (const std::string& v)
  : ValidatedStanzaExtension(EXT_TYPE),
    version(v)
{
  SetValid (true);
}

PongMessage::PongMessage (const gloox::Tag& t)
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (true);

  /* If the attribute is not present, then we assume an empty version.
     This is totally fine.  */
  version = t.findAttribute ("version");
}

const std::string&
PongMessage::filterString () const
{
  static const std::string filter = "/*/pong[@xmlns='" XMLNS "']";
  return filter;
}

gloox::StanzaExtension*
PongMessage::newInstance (const gloox::Tag* tag) const
{
  return new PongMessage (*tag);
}

gloox::StanzaExtension*
PongMessage::clone () const
{
  return new PongMessage (version);
}

gloox::Tag*
PongMessage::tag () const
{
  auto res = std::make_unique<gloox::Tag> ("pong");
  CHECK (res->setXmlns (XMLNS));
  if (!version.empty ())
    CHECK (res->addAttribute ("version", version));

  return res.release ();
}

/* ************************************************************************** */

SupportedNotifications::SupportedNotifications ()
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (false);
}

SupportedNotifications::SupportedNotifications (const std::string& s)
  : ValidatedStanzaExtension(EXT_TYPE),
    service(s)
{
  SetValid (true);
}

SupportedNotifications::SupportedNotifications (const gloox::Tag& t)
  : ValidatedStanzaExtension(EXT_TYPE)
{
  SetValid (false);

  service = t.findAttribute ("service");
  if (service.empty ())
    {
      LOG (WARNING) << "Empty / missing pubsub service";
      return;
    }

  for (const auto* child : t.findChildren ("notification"))
    {
      const std::string type = child->findAttribute ("type");
      if (type.empty ())
        {
          LOG (WARNING) << "Empty / missing notification type";
          continue;
        }

      const std::string node = child->cdata ();
      if (node.empty ())
        {
          LOG (WARNING) << "Empty / missing node name for type " << type;
          continue;
        }

      const auto res = notifications.emplace (type, node);
      LOG_IF (WARNING, !res.second) << "Duplicate notification type: " << type;
    }

  SetValid (true);
}

void
SupportedNotifications::AddNotification (const std::string& type,
                                         const std::string& node)
{
  CHECK (!type.empty ());
  CHECK (!node.empty ());

  const auto res = notifications.emplace (type, node);
  CHECK (res.second) << "Duplicate notification type: " << type;
}

const std::string&
SupportedNotifications::filterString () const
{
  static const std::string filter = "/*/notifications[@xmlns='" XMLNS "']";
  return filter;
}

gloox::StanzaExtension*
SupportedNotifications::newInstance (const gloox::Tag* tag) const
{
  return new SupportedNotifications (*tag);
}

gloox::StanzaExtension*
SupportedNotifications::clone () const
{
  auto res = std::make_unique<SupportedNotifications> ();

  if (IsValid ())
    {
      res->service = service;
      res->notifications = notifications;
      res->SetValid (true);
    }
  else
    res->SetValid (false);

  return res.release ();
}

gloox::Tag*
SupportedNotifications::tag () const
{
  CHECK (IsValid ()) << "Trying to serialise invalid SupportedNotifications";

  auto res = std::make_unique<gloox::Tag> ("notifications");
  CHECK (res->setXmlns (XMLNS));
  CHECK (res->addAttribute ("service", service));

  for (const auto& entry : notifications)
    {
      auto child = std::make_unique<gloox::Tag> ("notification", entry.second);
      CHECK (child->addAttribute ("type", entry.first));
      res->addChild (child.release ());
    }

  return res.release ();
}

/* ************************************************************************** */

NotificationUpdate::NotificationUpdate (const std::string& t,
                                        const Json::Value& s)
  : valid(true), type(t), newState(s)
{
  CHECK (!type.empty ());
}

NotificationUpdate::NotificationUpdate (const gloox::Tag& t)
  : valid(false)
{
  type = t.findAttribute ("type");
  if (type.empty ())
    {
      LOG (WARNING) << "Empty / missing update type";
      return;
    }

  if (!DecodeXmlJson (t, newState))
    return;

  valid = true;
}

std::unique_ptr<gloox::Tag>
NotificationUpdate::CreateTag () const
{
  CHECK (IsValid ()) << "Trying to serialise invalid NotificationUpdate";

  auto res = EncodeXmlJson ("update", newState);
  CHECK (res->setXmlns (XMLNS));
  CHECK (res->addAttribute ("type", type));

  return res;
}

/* ************************************************************************** */

} // namespace charon
