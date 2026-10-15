#!/usr/bin/env bash
# Skyien Automated Dependency Setup & Build Script
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo -e "\033[36m==================================================\033[0m"
echo -e "\033[1;36m       Skyien Setup & Installation Wizard       \033[0m"
echo -e "\033[36m==================================================\033[0m"

# 1. Directories setup
echo -e "\033[34m[*] Ensuring local directories...\033[0m"
mkdir -p "$HOME/.local/include" "$HOME/.local/lib" "$HOME/.local/bin" "$HOME/bin" "$DIR/lib" "$DIR/bin" "$HOME/.skyien/tdlib_data"

# 2. Check and fetch dependencies if missing
NEED_DEPS=0
if [ ! -f "$HOME/.local/lib/libtdjson.so" ] || [ ! -f "$HOME/.local/lib/libjsoncpp.so" ] || [ ! -f "$HOME/.local/lib/libsqlcipher.so" ]; then
    NEED_DEPS=1
fi

if [ $NEED_DEPS -eq 1 ]; then
    echo -e "\033[33m[*] Downloading missing dependencies via apt...\033[0m"
    TMP_DEBS="/tmp/skyien_debs_$$"
    TMP_EXTRACT="/tmp/skyien_extract_$$"
    mkdir -p "$TMP_DEBS" "$TMP_EXTRACT"

    pushd "$TMP_DEBS" > /dev/null
    apt download libtd-dev libtdjson1.8.38 libsqlcipher1 libjsoncpp-dev libjsoncpp26 libcurl4-openssl-dev
    for deb in *.deb; do
        dpkg -x "$deb" "$TMP_EXTRACT"
    done
    popd > /dev/null

    echo -e "\033[34m[*] Installing headers and libraries to ~/.local...\033[0m"
    cp -r "$TMP_EXTRACT/usr/include/jsoncpp/json" "$HOME/.local/include/"
    cp -r "$TMP_EXTRACT/usr/include/x86_64-linux-gnu/curl" "$HOME/.local/include/"
    cp -r "$TMP_EXTRACT/usr/include/TDLib1.8.38" "$HOME/.local/include/"
    mkdir -p "$HOME/.local/include/td/telegram"
    cp "$TMP_EXTRACT/usr/include/TDLib1.8.38/td/telegram/"* "$HOME/.local/include/td/telegram/"

    cp "$TMP_EXTRACT/usr/lib/x86_64-linux-gnu/TDLib1.8.38/libtdjson.so.1.8.38" "$HOME/.local/lib/"
    cp "$TMP_EXTRACT/usr/lib/x86_64-linux-gnu/libsqlcipher.so.1.8.0" "$HOME/.local/lib/"
    cp "$TMP_EXTRACT/usr/lib/x86_64-linux-gnu/libjsoncpp.so.1.9.6" "$HOME/.local/lib/"

    rm -rf "$TMP_DEBS" "$TMP_EXTRACT"
fi

# 3. Create symlinks in ~/.local/lib
echo -e "\033[34m[*] Configuring shared library symlinks...\033[0m"
ln -sf libtdjson.so.1.8.38 "$HOME/.local/lib/libtdjson.so"
ln -sf libtdjson.so.1.8.38 "$HOME/.local/lib/libtdjson.so.1"
ln -sf libsqlcipher.so.1.8.0 "$HOME/.local/lib/libsqlcipher.so.1"
ln -sf libsqlcipher.so.1 "$HOME/.local/lib/libsqlcipher.so"
ln -sf libjsoncpp.so.1.9.6 "$HOME/.local/lib/libjsoncpp.so.1"
ln -sf libjsoncpp.so.1 "$HOME/.local/lib/libjsoncpp.so"
ln -sf libjsoncpp.so.1.9.6 "$HOME/.local/lib/libjsoncpp.so.26"
if [ -f /usr/lib/x86_64-linux-gnu/libcurl.so.4 ]; then
    ln -sf /usr/lib/x86_64-linux-gnu/libcurl.so.4 "$HOME/.local/lib/libcurl.so"
fi

# Mirror into project lib/
cp -P "$HOME/.local/lib"/libtdjson* "$DIR/lib/"
cp -P "$HOME/.local/lib"/libsqlcipher* "$DIR/lib/"
cp -P "$HOME/.local/lib"/libjsoncpp* "$DIR/lib/"
cp -P "$HOME/.local/lib"/libcurl* "$DIR/lib/"

# 4. Prepare default configs if not present
if [ ! -f "$HOME/.skyien/config.json" ]; then
    echo -e "\033[34m[*] Creating ~/.skyien/config.json from example template...\033[0m"
    cp "$DIR/config.example.json" "$HOME/.skyien/config.json"
fi

if [ ! -f "$HOME/.skyien/targetor_config.json" ]; then
    echo -e "\033[34m[*] Creating ~/.skyien/targetor_config.json...\033[0m"
    cp "$DIR/targetor/targetor_config.json" "$HOME/.skyien/targetor_config.json"
fi

# 5. Build and install
echo -e "\033[36m[*] Compiling Skyien (all cores)...\033[0m"
make clean
make -j$(nproc)
make install

echo -e "\033[32m==================================================\033[0m"
echo -e "\033[1;32m[+] Setup complete! Skyien is ready to run.\033[0m"
echo -e "\033[32m==================================================\033[0m"
echo -e "You can now run: \033[1;37mskyien\033[0m or \033[1;37mskyien help\033[0m"
