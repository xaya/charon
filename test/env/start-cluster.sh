#!/bin/sh

# This is a utility script to start the test environment with a cluster of
# two ejabberd instances and clean it up afterwards.

docker-compose -f docker-compose-cluster.yml up -V --force-recreate --build
docker-compose -f docker-compose-cluster.yml rm -fv
