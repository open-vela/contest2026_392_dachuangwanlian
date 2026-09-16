#!/bin/bash

echo "do mklfs"
#./mklfs -c img -b 4096 -r 256 -p 256 -s 7340032 -i littlefs.bin
./mklittlefs -c img -b 4096 -s 22020096 littlefs.bin
echo -e -n "\x00\x00\xa0\x08" >>littlefs.bin
