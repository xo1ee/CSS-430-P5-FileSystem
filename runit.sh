#!/bin/bash

rm -f a.out
cp BFSDISK-clean-backup BFSDISK

gcc -Wall -Wextra -Wno-sign-compare *.c

./a.out
