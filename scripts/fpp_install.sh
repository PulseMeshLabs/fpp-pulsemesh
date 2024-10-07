#!/bin/bash

# fpp-pulsemesh install script

BASEDIR=$(dirname $0)
cd $BASEDIR
cd ..
make "SRCDIR=${SRCDIR}"

wget -O /usr/share/keyrings/pulsemsh-repo-key.gpg https://repo.pulsemesh.io/pulsemsh-repo-key.gpg
echo "deb [arch=amd64,arm64,armhf signed-by=/usr/share/keyrings/pulsemsh-repo-key.gpg] https://repo.pulsemesh.io stable main" | sudo tee /etc/apt/sources.list.d/pulsemsh.list

sudo apt update
sudo apt install -y pulsemesh-connector

sudo systemctl enable pulsemesh-connector.service
sudo systemctl start pulsemesh-connector.service

. "${FPPDIR}/scripts/common"
setSetting restartFlag 1
