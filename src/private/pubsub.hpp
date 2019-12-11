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

#ifndef CHARON_PUBSUB_HPP
#define CHARON_PUBSUB_HPP

#include <gloox/jid.h>
#include <gloox/message.h>
#include <gloox/messagehandler.h>
#include <gloox/pubsubmanager.h>
#include <gloox/tag.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace charon
{

class XmppClient;

/**
 * Implementation of the core XMPP pubsub logic that we need for implementing
 * state and pending notifications for Charon.  This is mostly a wrapper around
 * gloox' pubsub logic, exposing the simplified subset that we need in a
 * convenient form.
 *
 * This class is meant to be used through XmppClient and not instantiated
 * directly by itself.
 */
class PubSubImpl : private gloox::MessageHandler
{

public:

  /** Callback type for received published items.  */
  using ItemCallback = std::function<void (const gloox::Tag& t)>;

private:

  /** The underlying XmppClient.  */
  XmppClient& client;

  /** The gloox PubSub::Manager we use.  */
  gloox::PubSub::Manager manager;

  /** JID of the pubsub service.  */
  const gloox::JID service;

  /** Nodes owned by this pubsub instance.  */
  std::set<std::string> ownedNodes;

  /** Nodes subscribed to and the corresponding callbacks for items.  */
  std::map<std::string, ItemCallback> subscriptions;

  void handleMessage (const gloox::Message& msg,
                      gloox::MessageSession* session) override;

public:

  /**
   * Constructs the PubSub manager based on a given XmppClient instance.
   */
  explicit PubSubImpl (XmppClient& cl, const gloox::JID& s);

  /**
   * Sends requests to unsubscribe from all nodes we are subscribed to and to
   * delete all nodes that we own (without waiting for the results to arrive).
   */
  ~PubSubImpl ();

  PubSubImpl () = delete;
  PubSubImpl (const PubSubImpl&) = delete;
  void operator= (const PubSubImpl&) = delete;

  /**
   * Returns the pubsub service this is using.
   */
  const gloox::JID&
  GetService () const
  {
    return service;
  }

  /**
   * Creates a new instant node and returns its ID once done.
   * CHECK fails if node creation errors.
   */
  std::string CreateNode ();

  /**
   * Publishes a given tag to the given node.  It must be a node we own
   * (i.e. created before).
   */
  void Publish (const std::string& node, std::unique_ptr<gloox::Tag> data);

  /**
   * Subscribes to the given node.  Returns true on success, false on error.
   */
  bool SubscribeToNode (const std::string& node, const ItemCallback& cb);

};

} // namespace charon

#endif // CHARON_PUBSUB_HPP
