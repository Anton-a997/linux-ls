CC ?= gcc
CFLAGS ?= -Wall -Wextra -pedantic -std=gnu17

all: ls

ls: ls.o helpers.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f ls *.o

.PHONY: all clean
