# Makefile for Chaos-Proxy
# Professional C project build configuration

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -Werror -std=c99 -Iinclude -O2 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
DBGCFLAGS = -Wall -Wextra -g -DDEBUG -std=c99 -Iinclude -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
LDFLAGS  += -lm

SRC_DIR  = src
INC_DIR  = include
BUILD_DIR= build
OBJ_DIR  = $(BUILD_DIR)/obj
BIN_DIR  = $(BUILD_DIR)/bin

TARGET   = $(BIN_DIR)/chaos-proxy

# Automatically collect source and header files
SRCS     = $(wildcard $(SRC_DIR)/*.c)
OBJS     = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.c.o, $(SRCS))
DEPS     = $(OBJS:.o=.d)

.PHONY: all debug clean test help

all: $(TARGET)

# Main build target
$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo " [LD] $@"
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compile C source files into object files with dependency generation (-MMD -MP)
$(OBJ_DIR)/%.c.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo " [CC] $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Debug build target
debug: CFLAGS = $(DBGCFLAGS)
debug: all

# Create build directories
$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

# Clean artifacts
clean:
	@echo " [CLEAN] Removing build directory..."
	@rm -rf $(BUILD_DIR)

# Run automated tests
test: all
	@./test.sh

# Run desktop app
desktop:
	@echo "启动 Chaos-Proxy Desktop (Burp Suite Edition) 桌面前端工程..."
	@cd desktop && npm install && npm run dev

# Auto-include generated dependency files (.d)
-include $(DEPS)

help:
	@echo "Chaos-Proxy Build Options:"
	@echo "  make         - Build production release binary"
	@echo "  make debug   - Build debug binary with symbols and extra logs"
	@echo "  make test    - Build and run automated functionality tests"
	@echo "  make desktop - Run Vue 3 Desktop app (Burp Suite style)"
	@echo "  make clean   - Remove all compiled artifacts"
