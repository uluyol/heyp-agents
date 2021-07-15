#!/bin/bash

set -e

if ! [[ -e rspec.xml ]]; then
  echo first write cloudlab rspec to rspec.xml >&2
  exit 3
fi

exec bin/deploy-heyp update-config \
  -rspec rspec.xml \
  -subst CONNS=1,BPS=1,BE1_CONNS=0,BE1_BPS=1,BE2_CONNS=3,BE2_BPS=100,BE1_APPROVED_BPS=99,BE1_SURPLUS_BPS=11,BE2_APPROVED_BPS=0 \
  config-*.textproto
