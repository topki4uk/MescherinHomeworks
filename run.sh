#!/bin/bash
g++ main.cpp -fsanitize=address -o mytop
./mytop