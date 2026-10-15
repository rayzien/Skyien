#!/usr/bin/env bash
# High-speed build and install script for Skyien
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

# Ensure dependencies exist
if [ ! -f "$HOME/.local/lib/libtdjson.so" ] || [ ! -f "$HOME/.local/lib/libjsoncpp.so" ] || [ ! -f "$HOME/.local/lib/libsqlcipher.so" ]; then
    echo -e "\033[33m[*] Dependencies not detected. Running automated setup...\033[0m"
    ./setup.sh
    exit 0
fi

echo -e "\033[36m[*] Building Skyien...\033[0m"
make -j$(nproc)
make install

echo -e "\033[32m[+] Skyien is ready to use! Type 'skyien help' to get started.\033[0m"
