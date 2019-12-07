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
