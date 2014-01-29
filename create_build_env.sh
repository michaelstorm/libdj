#!/bin/bash
echo "deb http://http.debian.net/debian squeeze main" >> /etc/apt/sources.list
apt-get update
apt-get install g++ make cmake e2fslibs-dev
