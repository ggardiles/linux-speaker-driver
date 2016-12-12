#!/bin/sh
sudo rmmod spkr
sudo dmesg --clear
sudo make
sudo insmod spkr.ko
sudo dmesg -w
