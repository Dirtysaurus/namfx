#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fetches NeuralAmpModelerCore (MIT) incl. Eigen into third_party/.
set -e
cd "$(dirname "$0")/.."
mkdir -p third_party
if [ ! -f third_party/NeuralAmpModelerCore/NAM/dsp.h ]; then
  git clone --depth 1 https://github.com/sdatkinson/NeuralAmpModelerCore.git third_party/NeuralAmpModelerCore
fi
cd third_party/NeuralAmpModelerCore
if [ ! -f Dependencies/eigen/Eigen/Dense ]; then
  # Eigen lives on GitLab, which is sometimes overloaded -> fall back to the system package
  if ! git submodule update --init --depth 1 Dependencies/eigen; then
    echo "Eigen submodule failed, using the system libeigen3-dev instead"
    [ -f /usr/include/eigen3/Eigen/Dense ] || { sudo apt-get update; sudo apt-get install -y libeigen3-dev; }
    rm -rf Dependencies/eigen
    ln -s /usr/include/eigen3 Dependencies/eigen
  fi
fi
echo "OK: $(git log -1 --format='%h %cd' --date=short)"
