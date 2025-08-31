# Makefile for Amiga Multitask Library
# Assumes vbcc compiler is available

CC = vc
CFLAGS = -O2 -DAMIGA -I. -Wall -Wextra
LIBS = -lamiga -lsocket

# Library names
LIBNAME = multitask.library
STATIC_LIB = libmultitask.a

# Source files
SOURCES = multitask_lib.c amiga_networking.c
HEADERS = multitask_lib.h amiga_networking.h

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Default target - build static library
all: $(STATIC_LIB)

# Build the static library
$(STATIC_LIB): $(OBJECTS)
	ar rcs $(STATIC_LIB) $(OBJECTS)

# Compile source files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Note: Building as static library - no stubs needed

# Clean build files
clean:
	rm -f *.o $(STATIC_LIB) test_program test_networking

# Test programs
test: test_program test_networking
	./test_program
	./test_networking

# Build test programs
test_program: test_program.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -o test_program test_program.c $(STATIC_LIB) $(LIBS)

test_networking: test_networking.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -o test_networking test_networking.c $(STATIC_LIB) $(LIBS)

.PHONY: all clean test test_program test_networking
