#!/bin/bash

brew install texinfo wxwidgets
time (./build_release_macos.sh -d -a arm64 -x  && ./build_release_macos.sh -s -a arm64 -x)
