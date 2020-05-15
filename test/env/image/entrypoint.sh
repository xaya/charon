#!/bin/sh -e

ejabberdctl="${HOME}/bin/ejabberdctl"

echo "Starting ejabberd..."
${ejabberdctl} foreground &
pid=$!
while ! ${ejabberdctl} status
do
  sleep 1
done

# The container can be run in one of three modes:  As a single ejabberd
# server, as the "first" node in a two-node cluster (which registers the
# users) or as the "second" node in a two-node cluster (which connects
# to the first with a join_cluster command).
case $1 in
  solo)
    register="x"
    notify="x"
    ;;

  first)
    register="x"
    ;;

  second)
    join="ejabberd@first"
    notify="x"
    ;;

  *)
    echo "Invalid command specified: $1"
    exit 1
    ;;
esac

if [ -n "${register}" ]
then
  echo "Registering users..."
  ${ejabberdctl} register xmpptest1 localhost password
  ${ejabberdctl} register xmpptest2 localhost password
fi

if [ -n "${join}" ]
then
  echo "Joining cluster with ${join}..."
  while ! ${ejabberdctl} --node "${join}" status
  do
    sleep 1
  done
  ${ejabberdctl} --no-timeout join_cluster "${join}"
fi

if [ -n "${notify}" ]
then
  printf "\\n\\nXMPP testing environment started successfully\\n"
fi

wait ${pid}
