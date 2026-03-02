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

cmake -B ../vrt/vrtd/build -S ../vrt/vrtd -GNinja
cmake --build ../vrt/vrtd/build
sudo cmake --install ../vrt/vrtd/build

cmake -B ../vrt/build -S ../vrt -GNinja
cmake --build ../vrt/build
sudo cmake --install ../vrt/build

sudo systemctl start vrtd.service
