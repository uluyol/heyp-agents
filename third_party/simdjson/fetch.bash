#!/bin/bash

set -e

COMMIT=c96ff018fedc7fe087b6f898442458a31a240a28

curl https://raw.githubusercontent.com/simdjson/simdjson/${COMMIT}/singleheader/simdjson.h > simdjson.h
curl https://raw.githubusercontent.com/simdjson/simdjson/${COMMIT}/singleheader/simdjson.cpp > simdjson.cpp
