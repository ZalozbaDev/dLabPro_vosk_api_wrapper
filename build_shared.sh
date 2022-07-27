#!/bin/bash

# wget -q https://boostorg.jfrog.io/artifactory/main/release/1.76.0/source/boost_1_76_0.tar.gz
# tar xzf boost_1_76_0.tar.gz

rm -f libasr_server.so

g++ -shared -std=c++17 -O3 -fPIC -I./boost_1_76_0/ -I./inc/ -o libasr_server.so src/asr_server.cpp src/vosk_dlabpro_wrapper.c -lpthread -ldl
