# Makefile for Amiga Multitask Library
# Assumes vbcc compiler is available

CC = vc
CFLAGS = -c99 -O2 -DAMIGA
LIBS = -lamiga

# Library name
LIBNAME = multitask.library

# Source files
SOURCES = multitask_lib.c
STUBS = multitask_stubs.c
HEADERS = multitask_lib.h

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Default target
all: $(LIBNAME)

# Build the library
$(LIBNAME): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(LIBNAME) $(OBJECTS) $(LIBS)

# Compile source files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Build stubs
stubs: $(STUBS)
	$(CC) $(CFLAGS) -c $(STUBS) -o multitask_stubs.o

# Clean build files
clean:
	rm -f *.o $(LIBNAME)

# Install library to Amiga system
install: $(LIBNAME)
	cp $(LIBNAME) /Amiga/Libs/

# Test program
test: test_program
	./test_program

# Build test program
test_program: test_program.c multitask_stubs.o
	$(CC) $(CFLAGS) -o test_program test_program.c multitask_stubs.o $(LIBS)

.PHONY: all clean install test stubs
