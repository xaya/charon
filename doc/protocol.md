# Charon Protocol

Charon allows users to access game-state data from a GSP running on a
remote server through XMPP.  In other words, one or more GSP processes
can connect to an XMPP server and listen there for requests, and players
can connect as well using a custom XMPP client, which is then able to
send requests to the GSP connections.

In this document, we will give a brief overview of the underlying
protocol, i.e. what XMPP stanzas are exchanged in order to relay some
request from the player to the GSP (and the reply back).  For the examples,
we will assume that the player is `player@server/resource`, while the GSP
is connected as `gsp@server/resource`.

## Initial Connection

When a new player connects, they first look for a particular GSP instance
(i.e. resource) that they will then talk to.  This is necessary because
IQ stanzas will not be relayed to bare JIDs, and also so that the player
gets a consistent view of the game state in case the different GSPs are
slightly out-of-sync when they process a new block.

For this, the client initially sends an ordinary message stanza (not IQ)
to the GSPs bare JID `gsp@server`, asking for a reply from a suitable instance:

    <message to="gsp@server">
      <ping xmlns="https://xaya.io/charon/" />
    </message>

This will then be relayed by the XMPP server to one or more available GSP
connections (taking their priorities also into account).  A GSP client that
receives such a message and feels ready to accept another client (i.e. is not
overloaded) will reply with a directed presence as acknowledgement:

    <presence to="player@server/resource">
      <pong xmlns="https://xaya.io/charon/" />
    </presence>

The client can then select one of the replies it gets (in case there are
multiple) and record the GSP client's full JID (including its resource)
for further requests.  Once a server is selected, the client will send
a directed presence as well:

    <presence to="gsp@server/resource" />

By exchanging a pair of directed presence stanzas, the client and server
will temporarily subscribe to each other's presence, so that they will also
be notified about one becoming unavailable.
(And then e.g. the client can perform another handshake to find a different
server resource.)

## Ordinary RPC Calls

For an ordinary RPC call that should retrieve some data from the
current game state in a non-blocking way (i.e. *not* one of the `waitfor*`
methods), the player sends an IQ `get` request to the chosen GSP,
e.g. `gsp@server/resource`.  It has the following form:

    <iq type="get">
      <request xmlns="https://xaya.io/charon/">
        <method>METHOD</method>
        <params>PARAMS</params>
      </request>
    </iq>

Here, `METHOD` is just a string representing the method name of the RPC call.
`PARAMS` is also a string, which is the serialised JSON of the call parameters
(i.e. a JSON array or object).

The GSP responds with an IQ `result`.  For a successful call, it returns
the result of the JSON-RPC method:

    <iq type="result">
      <response xmlns="https://xaya.io/charon/">
        <result>RESULT</result>
      </response>
    </iq>

`RESULT` in this case is again a string of serialised JSON.  In case
of an error result from JSON-RPC, a stanza like this is returned instead:

    <iq type="result">
      <response xmlns="https://xaya.io/charon/">
        <error code="CODE">
          <message>MESSAGE</message>
          <data>DATA</data>
        </error>
      </response>
    </iq>

This holds the JSON-RPC error `CODE`, `MESSAGE` and `DATA` (the latter
again as serialised JSON).

**Note:**  Even for a JSON-RPC *error*, the IQ type is `result`.  IQ `error`s
would indicate an issue with the transport over XMPP, not a successful
transport but an error from the JSON-RPC call.

## Update Subscriptions

In addition to ordinary calls to get some state, GSPs also support
notifying a client for updates.  In particular, this is what is
behind the `waitforchange` and `waitforpendingchange` long-poll RPC methods.
Methods like that are supported specifically by Charon, by means of its
own subscription model based on
[XEP-0060 (PubSub)](https://xmpp.org/extensions/xep-0060.html).

When a server supports broadcasting updates of a certain type (e.g.
for the game state or pending moves), it creates one node per notification
type with its XMPP pubsub service.  Let's say the nodes are `node-state` and
`node-pending`, and the service is `pubsub.server`.

In this case, the server includes the notifications it supports in any "pong"
presence sent to a newly connected client (in addition to the `pong` tag):

    <presence to="player@server/resource">
      <pong xmlns="https://xaya.io/charon/" />
      <notifications xmlns="https://xaya.io/charon/" service="pubsub.service">
        <notification type="state">node-state</notification>
        <notification type="pending">node-pending</notification>
      </notifications>
    </presence>

Also, whenever the server detects an update to one of the states it provides
subscription notifications for, it publishes to the corresponding pubsub node
with a payload that contains the updated value as JSON data (in the same way
it is returned from the `waitfor*` RPC method):

    <item>
      <update xmlns="https://xaya.io/charon/" type="state">
        "NEW BEST BLOCK HASH"
      </update>
    </item>

    <item>
      <update xmlns="https://xaya.io/charon/" type="pending">
        {
          "version": 42,
          "pending": {"foo": "bar"}
        }
      </update>
    </item>

The Charon client, when it needs support for a particular RPC method like
`waitforchange`, will select a server that announces a pubsub node for the
required type.  It will then subscribe to updates on that node, and use this
to keep its own "copy" of the current state.  From that, it can handle
RPC methods just like an ordinary GSP would.
