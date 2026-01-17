# Makefile for Lab SOP4 C programs

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pthread
DEBUG_FLAGS = -g -O0
RELEASE_FLAGS = -O2

# Source files
SOURCES = Clock-sync.c Dice-sync.c Summary.c Task1.c Thread-pool-sync.c

# Executable names (remove .c extension)
EXECUTABLES = Clock-sync Dice-sync Summary Task1 Thread-pool-sync

# Default target
all: $(EXECUTABLES)

# Individual targets for each program
Clock-sync: Clock-sync.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $@ $<

Dice-sync: Dice-sync.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $@ $<

Summary: Summary.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $@ $<

Task1: Task1.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $@ $<

Thread-pool-sync: Thread-pool-sync.c
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $@ $<

# Debug versions
debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(EXECUTABLES)

# Clean target
clean:
	rm -f $(EXECUTABLES)

# Clean and rebuild
rebuild: clean all

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Compile all programs (default)"
	@echo "  debug        - Compile with debug flags"
	@echo "  clean        - Remove all executables"
	@echo "  rebuild      - Clean and rebuild all"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Individual programs:"
	@echo "  Clock-sync"
	@echo "  Dice-sync"
	@echo "  Summary"
	@echo "  Task1"
	@echo "  Thread-pool-sync"

# Mark phony targets
.PHONY: all debug clean rebuild help