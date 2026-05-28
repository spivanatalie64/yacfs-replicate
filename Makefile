CFLAGS = -Wall -Wextra -O3 -march=native -flto
LDFLAGS = -lpthread -flto

.PHONY: all clean install

all: yacfs-send yacfs-receive

yacfs-send: src/send.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

yacfs-receive: src/receive.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f yacfs-send yacfs-receive

install: yacfs-send yacfs-receive
	cp yacfs-send yacfs-receive /usr/local/bin/
