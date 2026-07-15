CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -Iinclude
TARGET := mini_rtos
TEST_TARGET := test_states
SRCS := src/main.c src/rtos.c src/scheduler.c src/context.c src/timer.c src/memory.c src/event.c
TEST_SRCS := tests/test_states.c src/rtos.c src/scheduler.c src/context.c src/timer.c src/memory.c src/event.c

.PHONY: all run test clean

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $(TEST_TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)
