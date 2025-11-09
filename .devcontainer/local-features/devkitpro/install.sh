#!/bin/bash

set -euo pipefail

apt-get update
apt-get install -y apt-transport-https

if ! [ -f /usr/local/share/keyring/devkitpro-pub.gpg ]; then
  mkdir -p /usr/local/share/keyring/
  curl -o /usr/local/share/keyring/devkitpro-pub.gpg https://apt.devkitpro.org/devkitpro-pub.gpg
fi
if ! [ -f /etc/apt/sources.list.d/devkitpro.list ]; then
  echo "deb [signed-by=/usr/local/share/keyring/devkitpro-pub.gpg] https://apt.devkitpro.org stable main" > /etc/apt/sources.list.d/devkitpro.list
fi

apt-get update
apt-get install -y devkitpro-pacman

ln -sf /proc/self/mounts /etc/mtab

dkp-pacman -Syu --noconfirm gba-dev

source /etc/profile.d/devkit-env.sh
