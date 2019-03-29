#/bin/bash
g++ -pthread -O2 main.cpp; nice -n -20 ./a.out
