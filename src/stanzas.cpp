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

#include "private/stanzas.hpp"

#include <glog/logging.h>

#include <memory>
#include <sstream>

namespace charon
{

namespace
{

/** XML namespace for our stanza extensions.  */
#define XMLNS "https://xaya.io/charon/"

/**
 * Parses the CData contained in a given tag into JSON.  Returns true
 * if the parsing was successful.
 */
bool
ParseJsonFromTag (const gloox::Tag& t, Json::Value& val)
{
  Json::CharReaderBuilder rbuilder;
  rbuilder["allowComments"] = false;
  rbuilder["strictRoot"] = false;
  rbuilder["failIfExtra"] = true;
  rbuilder["rejectDupKeys"] = true;

  std::istringstream in(t.cdata ());
  std::string parseErrs;
  if (!Json::parseFromStream (rbuilder, in, &val, &parseErrs))
    {
      LOG (WARNING)
          << "Failed parsing JSON:\n"
          << t.cdata () << "\n" << parseErrs;
      return false;
    }

  return true;
}

/**
 * Serialises the given JSON value into the CData of a new tag with
 * the given name and returns the newly created tag.
 */
std::unique_ptr<gloox::Tag>
SerialiseJsonToTag (const Json::Value& val, const std::string& tagName)
{
  Json::StreamWriterBuilder wbuilder;
  wbuilder["commentStyle"] = "None";
  wbuilder["indentation"] = "";
  wbuilder["enableYAMLCompatibility"] = false;
  wbuilder["dropNullPlaceholders"] = false;
  wbuilder["useSpecialFloats"] = false;

  const std::string serialised = Json::writeString (wbuilder, val);
  return std::make_unique<gloox::Tag> (tagName, serialised);
}

} // anonymous namespace

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
  if (!ParseJsonFromTag (*child, params))
    return;
  if (!params.isObject () && !params.isArray ())
    {
      LOG (WARNING) << "request params is neither object nor array";
      return;
    }

  SetValid (true);
}

const std::string&
RpcRequest::filterString () const
{
  static const std::string filter = "/iq/request[@xmlns='" XMLNS "']";
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

  child = SerialiseJsonToTag (params, "params");
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

      if (!ParseJsonFromTag (*outer, result))
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
  else if (!ParseJsonFromTag (*child, errorData))
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
  static const std::string filter = "/iq/response[@xmlns='" XMLNS "']";
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
      auto child = SerialiseJsonToTag (result, "result");
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
          auto child = SerialiseJsonToTag (errorData, "data");
          outer->addChild (child.release ());
        }

      res->addChild (outer.release ());
    }

  return res.release ();
}

/* ************************************************************************** */

} // namespace charon
