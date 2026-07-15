# ============================================================================
# Mini RTOS Simulator — Makefile
# ============================================================================
#
# Targets
# -------
#   make            Build the demo binary and the test binary (default).
#   make run        Build and run the demo application.
#   make test       Build and run the test suite.
#   make clean      Remove all build artefacts.
#   make format     Auto-format source files (requires clang-format).
#   make docs       Generate documentation (placeholder; requires Doxygen).
#   make help       Show this help message.
#
# Configuration
# -------------
#   CC      Compiler (default: gcc)
#   CFLAGS  Compiler flags (default: C99, full warnings)
# ============================================================================

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -Wpedantic -Iinclude

TARGET      := mini_rtos
TEST_TARGET := test_states

SRCS      := src/main.c src/rtos.c src/scheduler.c src/context.c \
             src/timer.c src/memory.c src/event.c
TEST_SRCS := tests/test_states.c src/rtos.c src/scheduler.c src/context.c \
             src/timer.c src/memory.c src/event.c

ALL_SRCS  := $(SRCS) tests/test_states.c

.PHONY: all run test clean format docs help

# ── Default target ──────────────────────────────────────────────────────────
all: $(TARGET) $(TEST_TARGET)

# ── Demo binary ─────────────────────────────────────────────────────────────
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# ── Test binary ─────────────────────────────────────────────────────────────
$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $(TEST_TARGET)

# ── Run the demo ─────────────────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET)

# ── Run the tests ────────────────────────────────────────────────────────────
test: $(TEST_TARGET)
	./$(TEST_TARGET)

# ── Remove build artefacts ───────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(TEST_TARGET)

# ── Auto-format (requires clang-format) ─────────────────────────────────────
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $(ALL_SRCS) include/*.h; \
		echo "clang-format applied."; \
	else \
		echo "clang-format not found; skipping format."; \
	fi

# ── Generate documentation (requires Doxygen) ────────────────────────────────
docs:
	@if command -v doxygen >/dev/null 2>&1; then \
		doxygen Doxyfile; \
	else \
		echo "Doxygen not found; skipping docs generation."; \
	fi

# ── Help ─────────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "Mini RTOS Simulator — available targets:"
	@echo ""
	@echo "  make          Build demo ($(TARGET)) and tests ($(TEST_TARGET))"
	@echo "  make run      Build and run the demo application"
	@echo "  make test     Build and run the test suite"
	@echo "  make clean    Remove build artefacts"
	@echo "  make format   Auto-format source files (requires clang-format)"
	@echo "  make docs     Generate documentation (requires Doxygen)"
	@echo "  make help     Show this help message"
	@echo ""
