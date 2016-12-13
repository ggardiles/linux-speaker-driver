#!/bin/bash

echo "dd if=songs.bin of=/dev/intspkr bs=$1 count=$2"
sudo sh -c "dd if=songs.bin of=/dev/intspkr bs=$1 count=$2"
