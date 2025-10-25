CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = 


# Target executables
TARGETS = jaineel gul

# Default target
all: $(TARGETS)

# Compile jaineel
jaineel: jaineel.c chat_common.h
	$(CC) $(CFLAGS) -o jaineel jaineel.c $(LDFLAGS)

# Compile gul
gul: gul.c chat_common.h
	$(CC) $(CFLAGS) -o gul gul.c $(LDFLAGS)


# Clean up compiled files
clean:
	rm -f $(TARGETS)

# Clean up system resources (shared memory and semaphores)
clean-resources:
	@echo "Cleaning up system resources..."
	@ipcrm -M 0x1234 2>/dev/null || true
	@ipcrm -S 0x5678 2>/dev/null || true
	@echo "Resources cleaned up."

# Clean everything
distclean: clean clean-resources

# Run jaineel
run-jaineel: jaineel
	@echo "Starting Jaineel..."
	./jaineel

# Run gul
run-gul: gul
	@echo "Starting Gul..."
	./gul

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Compile both jaineel and gul"
	@echo "  jaineel      - Compile jaineel only"
	@echo "  gul          - Compile gul only"
	@echo "  clean        - Remove compiled executables"
	@echo "  clean-resources - Clean up shared memory and semaphores"
	@echo "  distclean    - Clean everything"
	@echo "  run-jaineel  - Compile and run jaineel"
	@echo "  run-gul      - Compile and run gul"
	@echo "  analyze      - Run static code analysis"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  1. Run 'make all' to compile both programs"
	@echo "  2. In terminal 1: ./jaineel"
	@echo "  3. In terminal 2: ./gul"
	@echo "  4. Start chatting!"
	@echo ""
	@echo "Note: Either user can start first - the system will wait for both to connect."

analyze:
	@which scan-build >/dev/null 2>&1 || echo "Warning: scan-build not installed"
	@which cppcheck >/dev/null 2>&1 || echo "Warning: cppcheck not installed"
	scan-build make all
	cppcheck --enable=all *.c *.h

.PHONY: all clean clean-resources distclean run-jaineel run-gul help analyze
