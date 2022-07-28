#!/bin/bash

# wget -q https://boostorg.jfrog.io/artifactory/main/release/1.76.0/source/boost_1_76_0.tar.gz
# tar xzf boost_1_76_0.tar.gz

rm -f libasr-server.so

g++ -shared -std=c++17 -O3 -fPIC -I./boost_1_76_0/ -I./inc/ -I../dLabPro_vosk_api/programs/recognizer/ -o libasr-server.so src/asr_server.cpp src/vosk_dlabpro_wrapper.c -lpthread -ldl
