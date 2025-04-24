CC = gcc
CFLAGS = -Wall -Wextra -D_GNU_SOURCE
TARGETS = revproxy backend

all: $(TARGETS)

revproxy: revproxy.c
	$(CC) $(CFLAGS) -o $@ $<

backend: backend.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)

.PHONY: all clean