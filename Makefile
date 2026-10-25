CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE
CFLAGS += -Isrc
NCURSES := $(shell pkg-config --cflags ncursesw)
PULSE   := $(shell pkg-config --cflags libpulse)
LDLIBS  := $(shell pkg-config --libs libpulse-simple) -lncursesw -lm

all: vu

vu: src/main.c src/meter.c src/config.c src/backend.c
	$(CC) $(CFLAGS) $(NCURSES) $(PULSE) -o $@ $^ $(LDLIBS)

test: tests/meter_test tests/config_test
	./tests/meter_test
	./tests/config_test

tests/meter_test: tests/meter_test.c src/meter.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

tests/config_test: tests/config_test.c src/config.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -f vu tests/meter_test tests/config_test
