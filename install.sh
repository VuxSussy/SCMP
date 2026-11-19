#!/bin/bash
set -e

make clean
make
sudo make install

#create .scmp directory
mkdir -p ~/.scmp/colors

#copy over example files
cp -n examples/config ~/.scmp/
cp -n examples/rainbow ~/.scmp/colors/rainbow
