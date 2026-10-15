CXX ?= $(shell if command -v ccache >/dev/null 2>&1; then echo -n "ccache "; fi)$(shell if command -v clang++ >/dev/null 2>&1; then echo "clang++"; else echo "g++"; fi)
USE_LLD := $(shell if command -v lld >/dev/null 2>&1 && echo "$(CXX)" | grep -q "clang++"; then echo "-fuse-ld=lld"; fi)

CXXFLAGS ?= -std=c++20 -O2 -pipe $(USE_LLD) -Wall -Wextra \
	-Iinclude \
	-I$(HOME)/.local/include \
	-I$(HOME)/.local/include/TDLib1.8.38 \
	-I$(HOME)/.local/include/jsoncpp \
	-I/usr/include \
	-I/data/data/com.termux/files/usr/include

LDFLAGS ?= -L$(CURDIR)/lib \
	-L$(HOME)/.local/lib \
	-L$(HOME)/.local/lib/TDLib1.8.38 \
	-L/usr/lib/x86_64-linux-gnu \
	-L/data/data/com.termux/files/usr/lib \
	-Wl,-rpath,'$$ORIGIN/../lib' \
	-Wl,-rpath,$(HOME)/.local/lib \
	-Wl,-rpath,$(HOME)/.local/lib/TDLib1.8.38 \
	-Wl,--disable-new-dtags \
	-ltdjson -ljsoncpp -lcurl -lsqlcipher -lpthread

SRCS = src/Config.cpp src/TerminalUI.cpp src/TelegramClient.cpp src/AutomationEngine.cpp src/BotBridge.cpp src/Targetor.cpp src/GroupFinder.cpp src/UserMonitorDB.cpp src/UserCommandHandler.cpp src/main.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = bin/skyien

all: directories $(TARGET)

directories:
	@mkdir -p bin

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	@echo "\033[32m[+] Successfully built $(TARGET)\033[0m"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: $(TARGET)
	@mkdir -p $(HOME)/bin $(HOME)/.local/bin
	@rm -f $(HOME)/bin/skyien $(HOME)/.local/bin/skyien
	@cp $(TARGET) $(HOME)/bin/skyien
	@cp $(TARGET) $(HOME)/.local/bin/skyien
	@chmod +x $(HOME)/bin/skyien $(HOME)/.local/bin/skyien
	@echo "\033[32m[+] Installed skyien to $(HOME)/bin/skyien and $(HOME)/.local/bin/skyien\033[0m"

clean:
	rm -f src/*.o src/*.tmp $(TARGET)
	rm -rf build

.PHONY: all clean install directories
