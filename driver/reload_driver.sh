#!/bin/bash

make
sudo make install

echo 1 | sudo tee /sys/bus/pci/devices/0000\:1b\:00.1/remove
echo 1 | sudo tee /sys/bus/pci/devices/0000\:1b\:00.2/remove

sudo systemctl stop vrtd.service

sudo rmmod qdma-pf
sudo rmmod slash
sudo insmod /lib/modules/5.15.0-168-generic/extra/slash.ko

echo 1 | sudo tee /sys/bus/pci/rescan

sudo systemctl start vrtd.service
