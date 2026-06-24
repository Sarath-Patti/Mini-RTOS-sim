CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude
TARGET := mini_rtos
SRCS := src/main.c src/rtos.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
