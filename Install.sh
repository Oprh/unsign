#!/bin/bash
gcc -I ./unsign/include/ -o ./unsign/unsign ./unsign/main.c 
mv ./unsign/unsign /usr/local/bin/
