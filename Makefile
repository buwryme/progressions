# ────────────────────────────────────────────────────────────────────────────
#  Progressions — Arch Linux System Update Manager
#  Build: make  |  Install: make install  |  Clean: make clean
# ────────────────────────────────────────────────────────────────────────────

CXX      := clang++
TARGET   := progressions
BUILD_DIR := build
SOURCE_DIR := source

# Packages - GTKmm 4 is heavy, perfect for a PCH
PKGS     := gtkmm-4.0 libnotify
CXXFLAGS := -std=c++23 -O2 -Wall -Wextra -Wno-unused-parameter \
            $(shell pkg-config --cflags $(PKGS)) -I$(SOURCE_DIR)/inc
LDFLAGS  := $(shell pkg-config --libs $(PKGS))

# PCH Setup
PCH_SRC  := $(SOURCE_DIR)/inc/pch.hpp
PCH_OUT  := $(BUILD_DIR)/pch.hpp.pch

# Installation paths
BINDIR      := /usr/local/bin
DESKTOPDIR  := /usr/local/share/applications

SRCS := $(SOURCE_DIR)/main.cpp

.PHONY: all clean install uninstall compile_commands

all: compile_commands $(TARGET)

# Status logging helper (The bwyOS special)
define run_cmd
	@mkdir -p $(BUILD_DIR); \
	TMP=$(BUILD_DIR)/.cmd.$$RANDOM.out; \
	printf "  $(1)... "; \
	$(2) > $$TMP 2>&1; \
	status=$$?; \
	if [ $$status -eq 0 ]; then \
		echo "OK"; \
		rm -f $$TMP; \
	else \
		echo "ERR:"; \
		sed 's/^/    /' $$TMP; \
		rm -f $$TMP; \
		exit $$status; \
	fi
endef

# Generate a PCH source if it doesn't exist
$(PCH_SRC):
	@mkdir -p $(SOURCE_DIR)/inc
	@echo "#include <gtkmm.h>" > $(PCH_SRC)
	@echo "#include <glibmm.h>" >> $(PCH_SRC)
	@echo "✓ Created PCH header"

# Build PCH
$(PCH_OUT): $(PCH_SRC)
	$(call run_cmd,Compiling Precompiled Header,$(CXX) $(CXXFLAGS) -x c++-header $(PCH_SRC) -o $(PCH_OUT))

# The main build - Uses Bear to generate compile_commands.json
$(TARGET): $(SRCS) $(PCH_OUT)
	$(call run_cmd,Building $(TARGET),bear --append -- $(CXX) $(CXXFLAGS) -include-pch $(PCH_OUT) $(SRCS) -o $(TARGET) $(LDFLAGS))

# Explicitly trigger bear for a clean log
compile_commands:
	@rm -f compile_commands.json
	@echo "✓ Initializing compile_commands.json"

install: all
	@$(call run_cmd,Installing binary,install -Dm755 $(TARGET) $(BINDIR)/$(TARGET))
	@if [ -f progressions.desktop ]; then \
		install -Dm644 progressions.desktop $(DESKTOPDIR)/progressions.desktop; \
	fi

uninstall:
	@rm -f $(BINDIR)/$(TARGET)
	@rm -f $(DESKTOPDIR)/progressions.desktop
	@echo "✓ Uninstalled"

clean:
	$(call run_cmd,Cleaning build files,rm -rf $(BUILD_DIR) $(TARGET) compile_commands.json)
