#!/bin/sh

# This is to build and run from one command

# This will build in release mode

# Expects to be executed from main dir

echo -e "\e[1m\e[32mRunning DotCVM in release mode\e[0m\e[39m"

./buildRelease.sh && build/Release/DotCVM