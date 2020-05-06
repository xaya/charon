# Constructs a Docker image of ejabberd with custom configuration to run
# a local XMPP node for testing Charon against.

FROM ejabberd/ecs
USER root

WORKDIR $HOME
COPY ejabberd.yml ejabberdctl.cfg conf/
COPY run.sh bin/

# Since this is just temporary for testing, we might as well just
# kill the server quickly.
STOPSIGNAL SIGKILL

USER ejabberd
ENTRYPOINT ["/home/ejabberd/bin/run.sh"]
