# Compiler and Compile options.
C = gcc
CFLAGS = -g -Wall

# Macros specifying path for compile.
SRCS := $(wildcard src/*.c) 
OBJS := $(SRCS:.c=.o)
BIN = ./bin/

# Compile command.
TARGET = shell
$(TARGET):$(OBJS)
		mkdir -p $(BIN)
		$(CC) -o $(BIN)$(TARGET) $(SRCS)

# Delete binary & object files.
clean:
	rm -f $(BIN)$(TARGET) $(OBJS)
	rm -df $(BIN)

