#!/bin/sh
# This is not set as exectuable and shouldn't be called.

echo "Hooked" > "$(dirname "$0")/../../../postupdate_not_hooked"
