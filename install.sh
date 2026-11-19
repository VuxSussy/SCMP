#!/bin/bash
set -e

make clean
make
sudo make install

#create .scmp directory
mkdir -p ~/.scmp/colors

#install the bundled plugins (enable them with P in scmp)
make plugins
mkdir -p ~/.scmp/plugins
cp build/plugins/* ~/.scmp/plugins/

#copy over example files
cp -n examples/config ~/.scmp/
cp -n examples/rainbow ~/.scmp/colors/rainbow
