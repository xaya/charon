#!/bin/bash

# This is a utility script to start the test environment and clean it
# up afterwards.

docker-compose up -V --force-recreate
docker-compose rm -fv
