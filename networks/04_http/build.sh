#!/bin/sh
set -e
mkdir -p bin
c++ -std=c++17 -O2 -Wall -Wextra src/main.cc -o bin/http
