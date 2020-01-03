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

#ifndef CHARON_STANZAS_HPP
#define CHARON_STANZAS_HPP

#include <gloox/stanzaextension.h>
#include <gloox/tag.h>

#include <json/json.h>

#include <map>
#include <string>

namespace charon
{

/**
 * A general gloox StanzaExtension which has a "valid" flag.  This allows us
 * to check incoming stanzas for whether or not they have been parsed correctly.
 */
class ValidatedStanzaExtension : public gloox::StanzaExtension
{

private:

  /** Whether or not the incoming data was valid.  */
  bool valid = false;

protected:

  explicit ValidatedStanzaExtension (const int type)
    : StanzaExtension(type)
  {}

  /**
   * Sets the valid flag.
   */
  void
  SetValid (const bool v)
  {
    valid = v;
  }

public:

  /**
   * Returns whether or not the data is valid.
   */
  bool
  IsValid () const
  {
    return valid;
  }

};

/**
 * A gloox StanzaExtension representing a JSON-RPC method call / request
 * as part of an IQ stanza.  In XML, this is represented by a tag of
 * the following form:
 *
 *  <request xmlns="https://xaya.io/charon/">
 *    <method>mymethod</method>
 *    <params>["json params", 42]</params>
 *  </request>
 */
class RpcRequest : public ValidatedStanzaExtension
{

private:

  /** The method name being called.  */
  std::string method;

  /** The params data for the call.  */
  Json::Value params;

public:

  /** Extension type for RPC request extensions.  */
  static constexpr int EXT_TYPE = gloox::ExtUser + 1;

  /**
   * Constructs an empty instance (for use as factory).  It will be marked
   * as invalid.
   */
  RpcRequest ();

  /**
   * Constructs an instance with the given data.
   */
  explicit RpcRequest (const std::string& m, const Json::Value& p);

  /**
   * Constructs an instance from a given tag.
   */
  explicit RpcRequest (const gloox::Tag& t);

  const std::string&
  GetMethod () const
  {
    return method;
  }

  const Json::Value&
  GetParams () const
  {
    return params;
  }

  const std::string& filterString () const override;
  gloox::StanzaExtension* newInstance (const gloox::Tag* tag) const override;
  gloox::StanzaExtension* clone () const override;
  gloox::Tag* tag () const override;

};

/**
 * A gloox StanzaExtension representing a JSON-RPC call response (including
 * errors from JSON-RPC).  In XML, this is represented by a tag of
 * the following form:
 *
 *  <response xmlns="https://xaya.io/charon/">
 *    <result>{"some": "json result"}</result>
 *  </response>
 *
 *  <response xmlns="https://xaya.io/charon/">
 *    <error code="42">
 *      <message>error message</message>
 *      <data>["extra", "json data"]</data>
 *    </error>
 *  </response>
 */
class RpcResponse : public ValidatedStanzaExtension
{

private:

  /** If this is a success response.  */
  bool success;

  /** On success, the result data.  */
  Json::Value result;

  /** On error, the error code.  */
  int errorCode;
  /** On error, the error message.  */
  std::string errorMsg;
  /** On error, the extra data.  */
  Json::Value errorData;

public:

  /** Extension type for RPC response extensions.  */
  static constexpr int EXT_TYPE = gloox::ExtUser + 2;

  /**
   * Constructs an empty instance (for use as factory).  It will be marked
   * as invalid.
   */
  RpcResponse ();

  /**
   * Constructs an instance for success with the given result.
   */
  explicit RpcResponse (const Json::Value& res);

  /**
   * Constructs an instance for error with the given data.
   */
  explicit RpcResponse (int c, const std::string& msg, const Json::Value& d);

  /**
   * Constructs an instance from a given tag.
   */
  explicit RpcResponse (const gloox::Tag& t);

  bool
  IsSuccess () const
  {
    return success;
  }

  const Json::Value& GetResult () const;

  int GetErrorCode () const;
  const std::string& GetErrorMessage () const;
  const Json::Value& GetErrorData () const;

  const std::string& filterString () const override;
  gloox::StanzaExtension* newInstance (const gloox::Tag* tag) const override;
  gloox::StanzaExtension* clone () const override;
  gloox::Tag* tag () const override;

};

/**
 * A gloox StanzaExtension representing a "ping" message:
 *
 *  <ping xmlns="https://xaya.io/charon/" />
 */
class PingMessage : public ValidatedStanzaExtension
{

public:

  /** Extension type for ping extensions.  */
  static constexpr int EXT_TYPE = gloox::ExtUser + 3;

  /**
   * Constructs an empty instance, which can be used as a factory
   * as well as a totally valid message.
   */
  PingMessage ();

  const std::string& filterString () const override;
  gloox::StanzaExtension* newInstance (const gloox::Tag* tag) const override;
  gloox::StanzaExtension* clone () const override;
  gloox::Tag* tag () const override;

};

/**
 * A gloox StanzaExtension representing a "pong" message/presence:
 *
 *  <pong xmlns="https://xaya.io/charon/" />
 */
class PongMessage : public ValidatedStanzaExtension
{

public:

  /** Extension type for pong extensions.  */
  static constexpr int EXT_TYPE = gloox::ExtUser + 4;

  /**
   * Constructs an empty instance, which can be used as a factory
   * as well as a totally valid message.
   */
  PongMessage ();

  const std::string& filterString () const override;
  gloox::StanzaExtension* newInstance (const gloox::Tag* tag) const override;
  gloox::StanzaExtension* clone () const override;
  gloox::Tag* tag () const override;

};

/**
 * Gloox StanzaExtension for the supported notifications and PubSub nodes
 * of a Charon server (sent together with a pong presence):
 *
 *  <notifications xmlns="https://xaya.io/charon/" service="pubsub.service">
 *    <notification type="state">node-state</notification>
 *    <notification type="pending">node-pending</notification>
 *  </notifications>
 */
class SupportedNotifications : public ValidatedStanzaExtension
{

private:

  /** The indicated PubSub service.  */
  std::string service;

  /** Notification nodes keyed by the type string.  */
  std::map<std::string, std::string> notifications;

public:

  /** Extension type for notifications data extensions.  */
  static constexpr int EXT_TYPE = gloox::ExtUser + 5;

  /**
   * Constructs an empty instance (for use as factory).  It will be marked
   * as invalid.
   */
  SupportedNotifications ();

  /**
   * Constructs an instance for the given service and (for now) an empty
   * set of notifications.
   */
  explicit SupportedNotifications (const std::string& s);

  /**
   * Constructs an instance from a given tag.
   */
  explicit SupportedNotifications (const gloox::Tag& t);

  /**
   * Returns the PubSub service.
   */
  const std::string&
  GetService () const
  {
    return service;
  }

  /**
   * Returns the map of notification types to pubsub nodes.
   */
  const std::map<std::string, std::string>&
  GetNotifications () const
  {
    return notifications;
  }

  /**
   * Adds a notification type to the internal map.  The type must not yet be
   * set, else this is an error.
   */
  void AddNotification (const std::string& type, const std::string& node);

  const std::string& filterString () const override;
  gloox::StanzaExtension* newInstance (const gloox::Tag* tag) const override;
  gloox::StanzaExtension* clone () const override;
  gloox::Tag* tag () const override;

};

/**
 * Wrapper around an "update" payload for the notification items.  This is not
 * exactly a StanzaExtension (as pubsub payloads are not handled by gloox
 * in that way), but has a similar interface and usage.  The tag this
 * represents looks like this:
 *
 *  <update xmlns="https://xaya.io/charon/" type="state">
 *    JSON string of new state
 *  </update>
 */
class NotificationUpdate
{

private:

  /** Whether or not this is valid.  */
  bool valid;

  /** The update's type string.  */
  std::string type;

  /** The new JSON data.  */
  Json::Value newState;

public:

  /**
   * Constructs an instance for the given data.
   */
  explicit NotificationUpdate (const std::string& t, const Json::Value& s);

  /**
   * Constructs an instance by parsing the given tag.
   */
  explicit NotificationUpdate (const gloox::Tag& t);

  NotificationUpdate () = delete;
  NotificationUpdate (const NotificationUpdate&) = delete;
  void operator= (const NotificationUpdate&) = delete;

  /**
   * Returns whether or not the data is valid.
   */
  bool
  IsValid () const
  {
    return valid;
  }

  /**
   * Returns the type string.
   */
  const std::string&
  GetType () const
  {
    return type;
  }

  /**
   * Returns the new JSON data / state.
   */
  const Json::Value&
  GetState () const
  {
    return newState;
  }

  /**
   * Serialises the object into a tag.
   */
  std::unique_ptr<gloox::Tag> CreateTag () const;

};

} // namespace charon

#endif // CHARON_STANZAS_HPP
