#!/bin/sh

# This is a utility script to start the test environment with a single
# ejabberd instance and clean it up afterwards.

docker-compose -f docker-compose-solo.yml up -V --force-recreate --build
docker-compose -f docker-compose-solo.yml rm -fv
