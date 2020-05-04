#!/bin/sh -e

ejabberdctl="${HOME}/bin/ejabberdctl"

echo "Starting ejabberd..."
${ejabberdctl} foreground &
pid=$!
while ! ${ejabberdctl} status
do
  sleep 1
done

echo "Registering users..."
${ejabberdctl} register xmpptest1 localhost password
${ejabberdctl} register xmpptest2 localhost password

printf "\\n\\nXMPP testing environment started successfully\\n"
wait ${pid}
