# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -Wno-error -g -DLINUX_MODE

# Target executable
TARGET = sim_linux

# Source and header files
SRCS = main.c sim_source.c core.c core_source.c
HEADERS = sim_source.h core_source.h

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile source files into object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up generated files
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean
