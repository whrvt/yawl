#!/usr/bin/env bash
# Bootstrap the build system

set -e

mkdir -p m4

chmod +x build-aux/download-deps.sh

autoreconf -fiv

echo "Configuration files generated."
echo "Run './configure' to set up the Makefile."
echo "Then, just run 'make' to build yawl."

exit 0
