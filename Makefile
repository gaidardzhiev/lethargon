CC:=$(shell command -v musl-gcc 2>/dev/null || command -v tcc 2>/dev/null || command -v gcc 2>/dev/null)
FLAGS=-static -no-pie -g -Wall -Wextra
BIN=lethargon
XFLAGS=-g -Wall -Wextra
ifeq ($(findstring tcc,$(CC)),tcc)
FLAGS=$(XFLAGS)
else
FLAGS=$(XFLAGS) -static -no-pie
endif
ifeq ($(strip $(CC)),)
CC=cc
endif

all: $(BIN)

$(BIN): lethargon.c lethargon.h
	$(CC) -o $@ lethargon.c $(FLAGS)

install:
	cp $(BIN) /usr/local/bin/$(BIN)

clean:
	rm -f $(BIN) src/*.out
