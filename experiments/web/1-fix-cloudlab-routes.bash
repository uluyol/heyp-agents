#!/bin/bash

set -e

echo "ONLY RUN THIS ONE TIME PER CLOUDLAB CLUSTER"
bin/deploy-heyp cloudlab-dualnet-fix -v
