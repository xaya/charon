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

#include "private/pubsub.hpp"

#include "private/xmppclient.hpp"

#include <gloox/clientbase.h>
#include <gloox/pubsubevent.h>
#include <gloox/pubsubitem.h>
#include <gloox/pubsubresulthandler.h>

#include <glog/logging.h>

#include <condition_variable>
#include <mutex>

namespace charon
{

namespace
{

/**
 * PubSub::ResultHandler instance that defines all methods but CHECK's that
 * they are not called.  This allows for convenient subclassing where we only
 * have to actually override the methods appropriate for what results we
 * expect from the call.
 */
class GeneralResultHandler : public gloox::PubSub::ResultHandler
{

public:

  GeneralResultHandler () = default;

#define UNSUPPORTED_METHOD(name, args) \
  void name args override \
  { \
    LOG (FATAL) << "Method " #name " is not supported"; \
  }

  UNSUPPORTED_METHOD (handleItem,
      (const gloox::JID&, const std::string&, const gloox::Tag*));
  UNSUPPORTED_METHOD (handleItems,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::PubSub::ItemList&, const gloox::Error*));
  UNSUPPORTED_METHOD (handleItemPublication,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::PubSub::ItemList&, const gloox::Error*));
  UNSUPPORTED_METHOD (handleItemDeletion,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::PubSub::ItemList&, const gloox::Error*));

  UNSUPPORTED_METHOD (handleSubscriptionResult,
      (const std::string&, const gloox::JID&, const std::string&,
       const std::string&, const gloox::JID&,
       gloox::PubSub::SubscriptionType, const gloox::Error*));
  UNSUPPORTED_METHOD (handleUnsubscriptionResult,
      (const std::string&, const gloox::JID&, const gloox::Error*));
  UNSUPPORTED_METHOD (handleSubscriptionOptions,
      (const std::string&, const gloox::JID&, const gloox::JID&,
       const std::string&, const gloox::DataForm*,
       const std::string&, const gloox::Error*));
  UNSUPPORTED_METHOD (handleSubscriptionOptionsResult,
      (const std::string&, const gloox::JID&, const gloox::JID&,
       const std::string&, const std::string&, const gloox::Error*));

  UNSUPPORTED_METHOD (handleSubscribers,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::PubSub::SubscriptionList&, const gloox::Error*));
  UNSUPPORTED_METHOD (handleSubscribersResult,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::PubSub::SubscriberList*, const gloox::Error*));
  UNSUPPORTED_METHOD (handleAffiliates,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::PubSub::AffiliateList*, const gloox::Error*));
  UNSUPPORTED_METHOD (handleAffiliatesResult,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::PubSub::AffiliateList*, const gloox::Error*));

  UNSUPPORTED_METHOD (handleNodeConfig,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::DataForm*, const gloox::Error*));
  UNSUPPORTED_METHOD (handleNodeConfigResult,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::Error*));

  UNSUPPORTED_METHOD (handleNodeCreation,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::Error*));
  UNSUPPORTED_METHOD (handleNodeDeletion,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::Error*));
  UNSUPPORTED_METHOD (handleNodePurge,
      (const std::string&, const gloox::JID&, const std::string&,
       const gloox::Error*));

  UNSUPPORTED_METHOD (handleSubscriptions,
      (const std::string&, const gloox::JID&,
       const gloox::PubSub::SubscriptionMap&, const gloox::Error*));
  UNSUPPORTED_METHOD (handleAffiliations,
      (const std::string&, const gloox::JID&,
       const gloox::PubSub::AffiliationMap&, const gloox::Error*));
  UNSUPPORTED_METHOD (handleDefaultNodeConfig,
      (const std::string&, const gloox::JID&, const gloox::DataForm*,
       const gloox::Error*));

#undef UNSUPPORTED_METHOD

};

/**
 * ResultHandler that accepts node deletion and unsubscription results
 * (as we get them for cleaning up a PubSubImpl).
 */
class CleanUpResultHandler : public GeneralResultHandler
{

public:

  void
  handleNodeDeletion (const std::string& id, const gloox::JID& service,
                      const std::string& node,
                      const gloox::Error* error) override
  {
    if (error == nullptr)
      VLOG (1) << "Node " << node << " has been deleted";
    else
      LOG (ERROR) << "Error deleting node " << node << ": " << error->text ();
  }

  void
  handleUnsubscriptionResult (const std::string& id, const gloox::JID& service,
                              const gloox::Error* error) override
  {
    if (error == nullptr)
      VLOG (1) << "Unsubscribed from node";
    else
      LOG (ERROR) << "Error unsubscribing: " << error->text ();
  }

};

} // anonymous namespace

/**
 * ResultHandler that notifies a condition variable and sets a flag
 * when done.
 */
class WaiterResultHandler : public GeneralResultHandler
{

private:

  /**
   * Associated PubSub instance.  We add ourselves to its list of
   * waiting handlers on construction and remove ourselves again
   * in the destructor.
   */
  PubSubImpl& pubsub;

  /** Mutex for the condition variable and flag.  */
  std::mutex mut;

  /** Condition variable to wait on.  */
  std::condition_variable cv;

  /** Whether or not we are done.  */
  bool done = false;

public:

  explicit WaiterResultHandler (PubSubImpl& p)
    : pubsub(p)
  {
    std::lock_guard<std::mutex> lock(pubsub.mutWaitingHandlers);
    pubsub.waitingHandlers.insert (this);
  }

  WaiterResultHandler () = delete;
  WaiterResultHandler (const WaiterResultHandler&) = delete;
  void operator= (const WaiterResultHandler&) = delete;

  ~WaiterResultHandler ()
  {
    std::lock_guard<std::mutex> lock(pubsub.mutWaitingHandlers);
    pubsub.waitingHandlers.erase (this);
    pubsub.cvWaitingHandlers.notify_all ();
  }

  /**
   * Signal the action as done.  This should be called from subclasses
   * from the respective handle... method.  It may also be called
   * from the PubSubImpl when the connection is closed down (and thus
   * we won't get any responses anymore).
   */
  void
  Notify ()
  {
    std::lock_guard<std::mutex> lock(mut);
    done = true;
    cv.notify_all ();
  }

  /**
   * Wait for the result to be done.
   */
  void
  Wait ()
  {
    std::unique_lock<std::mutex> lock(mut);
    while (!done)
      cv.wait (lock);
  }

};

namespace
{

/**
 * ResultHandler that waits for a node creation confirmation.
 */
class NodeCreationResultHandler : public WaiterResultHandler
{

private:

  /** The created node's name.  */
  std::string node;

public:

  explicit NodeCreationResultHandler (PubSubImpl& p)
    : WaiterResultHandler(p)
  {}

  void
  handleNodeCreation (const std::string& id, const gloox::JID& service,
                      const std::string& n, const gloox::Error* error) override
  {
    CHECK (error == nullptr) << "Error creating node: " << error->text ();
    VLOG (1) << "Successfully created node " << n;
    node = n;
    Notify ();
  }

  const std::string&
  GetNode () const
  {
    return node;
  }

};

/**
 * ResultHandler that waits for an item publication confirmation.
 */
class ItemPublicationResultHandler : public WaiterResultHandler
{

public:

  explicit ItemPublicationResultHandler (PubSubImpl& p)
    : WaiterResultHandler(p)
  {}

  void
  handleItemPublication (const std::string& id, const gloox::JID& service,
                         const std::string& node,
                         const gloox::PubSub::ItemList& items,
                         const gloox::Error* error) override
  {
    CHECK (error == nullptr)
        << "Error publishing to " << node << ": " << error->text ();
    VLOG (1) << "Successfully published to " << node;
    Notify ();
  }

};

/**
 * ResultHandler that waits for a node subscription confirmation.
 */
class NodeSubscriptionResultHandler : public WaiterResultHandler
{

private:

  /** Whether or not subscription was successful.  */
  bool success = false;

public:

  explicit NodeSubscriptionResultHandler (PubSubImpl& p)
    : WaiterResultHandler(p)
  {}

  void
  handleSubscriptionResult (const std::string& id, const gloox::JID& service,
                            const std::string& node, const std::string& sid,
                            const gloox::JID& jid,
                            const gloox::PubSub::SubscriptionType subType,
                            const gloox::Error* error) override
  {
    if (error != nullptr)
      {
        LOG (ERROR)
            << "Error subscribing to " << node << ": " << error->text ();
        success = false;
      }
    else if (subType != gloox::PubSub::SubscriptionSubscribed)
      {
        LOG (ERROR)
            << "Subscription status for node " << node << ": " << subType;
        success = false;
      }
    else
      {
        VLOG (1) << "Successfully subscribed to " << node;
        success = true;
      }

    Notify ();
  }

  bool
  GetSuccess () const
  {
    return success;
  }

};

} // anonymous namespace

PubSubImpl::PubSubImpl (XmppClient& cl, const gloox::JID& s)
  : client(cl), manager(&client.client), service(s)
{
  client.RunWithClient ([this] (gloox::Client& c)
    {
      c.registerStanzaExtension (new gloox::PubSub::Event ());
      c.registerMessageHandler (this);
    });
}

PubSubImpl::~PubSubImpl ()
{
  client.RunWithClient ([this] (gloox::Client& c)
    {
      c.removeMessageHandler (this);

      CleanUpResultHandler handler;

      LOG (INFO)
          << "Unsubscribing from " << subscriptions.size () << " nodes...";
      for (const auto& entry : subscriptions)
        manager.removeID (manager.unsubscribe (service, entry.first, "",
                                               &handler));

      LOG (INFO) << "Deleting " << ownedNodes.size () << " owned nodes...";
      for (const auto& node : ownedNodes)
        manager.removeID (manager.deleteNode (service, node, &handler));
    });

  /* Notify all handlers currently waiting for a server reply that it won't
     come anymore (because we are shutting down / disconnecting) and make sure
     to wait until all of them are done working with this instance.  */
  {
    std::unique_lock<std::mutex> lock(mutWaitingHandlers);
    for (auto* h : waitingHandlers)
      h->Notify ();

    while (!waitingHandlers.empty ())
      cvWaitingHandlers.wait (lock);
  }
}

void
PubSubImpl::handleMessage (const gloox::Message& msg,
                           gloox::MessageSession* session)
{
  const auto* pse
      = msg.findExtension<gloox::PubSub::Event> (gloox::ExtPubSubEvent);
  if (pse == nullptr || pse->type () != gloox::PubSub::EventItems)
    return;

  LOG (INFO)
      << "Received pubsub item for node " << pse->node ()
      << " from " << msg.from ().full ();
  for (const auto& itm : pse->items ())
    {
      if (itm->retract)
        continue;

      VLOG (1) << "Item XML:\n" << itm->payload->xml ();
      const auto mit = subscriptions.find (pse->node ());
      if (mit == subscriptions.end ())
        LOG (WARNING)
            << "Ignoring item for non-subscribed node " << pse->node ();
      else
        mit->second (*itm->payload);
    }
}

std::string
PubSubImpl::CreateNode ()
{
  NodeCreationResultHandler handler(*this);
  std::string id;
  client.RunWithClient ([&] (gloox::Client& c)
    {
      id = manager.createNode (service, "", nullptr, &handler);
    });
  CHECK (!id.empty ());
  handler.Wait ();

  const auto& node = handler.GetNode ();
  if (node.empty ())
    LOG (WARNING) << "Failed to create pubsub node";
  else
    ownedNodes.insert (node);

  /* Be extra safe and make sure that the handler is no longer tracked by
     gloox before it goes out of scope.  It will typically be removed already
     when the response is processed, but if e.g. the connection was closed
     and the handler woken because of that, it will still be around.  */
  manager.removeID (id);

  return node;
}

void
PubSubImpl::Publish (const std::string& node, std::unique_ptr<gloox::Tag> data)
{
  CHECK_GT (ownedNodes.count (node), 0)
      << "Can't publish to non-owned node " << node;

  auto item = std::make_unique<gloox::PubSub::Item> ();
  item->setPayload (data.release ());
  gloox::PubSub::ItemList items;
  items.push_back (item.release ());

  ItemPublicationResultHandler handler(*this);
  std::string id;
  client.RunWithClient ([&] (gloox::Client& c)
    {
      id = manager.publishItem (service, node, items, nullptr, &handler);
    });
  CHECK (!id.empty ());
  handler.Wait ();

  manager.removeID (id);
}

bool
PubSubImpl::SubscribeToNode (const std::string& node, const ItemCallback& cb)
{
  NodeSubscriptionResultHandler handler(*this);
  std::string id;
  client.RunWithClient ([&] (gloox::Client& c)
    {
      id = manager.subscribe (service, node, &handler);
    });

  if (id.empty ())
    return false;

  handler.Wait ();
  
  const bool ok = handler.GetSuccess ();
  if (ok)
    subscriptions.emplace (node, cb);

  manager.removeID (id);
  return ok;
}

} // namespace charon
