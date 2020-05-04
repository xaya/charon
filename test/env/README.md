# Testing Environment

For testing Charon, a suitable XMPP server must be running.  This directory
provides a [Docker Compose](https://docs.docker.com/compose/) configuration
for starting such a server on localhost.

When run, it starts up a local ejabberd instance for `localhost`
and with pubsub enabled on `pubsub.localhost`.  Two user accounts
are available on it, `xmpptest1` and `xmpptest2`, the password for
both accounts is `password`.

To start up the test environment and clean up the temporary data in
volumes after it is finished, use simply

    $ ./run.sh
