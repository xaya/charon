# Constructs a Docker image of ejabberd with custom configuration to run
# a local XMPP node for testing Charon against.
#
# This uses a variant of ejabberd/ecs without declared VOLUME's to bypass
# issues with that.  For this, https://github.com/gdraheim/docker-copyedit
# can be used.

FROM ejabberd-unvolumed
USER root

ARG ERLANG_HOST="localhost"

WORKDIR $HOME
COPY ejabberd.yml ejabberdctl.cfg ejabberd.pem conf/
COPY entrypoint.sh bin/
RUN echo "ERLANG_NODE=ejabberd@${ERLANG_HOST}" >>conf/ejabberdctl.cfg

# Since this is just temporary for testing, we might as well just
# kill the server quickly.
STOPSIGNAL SIGKILL

USER ejabberd
VOLUME ["/home/ejabberd/database"]
ENTRYPOINT ["/home/ejabberd/bin/entrypoint.sh"]
