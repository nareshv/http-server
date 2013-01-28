.PHONY: help run build benchmark clean

help:
        @echo "syntax: make (build|run)"

CFLAGS=-Wall -Werror
LIBS=-lpthread
CC=gcc

build:
        gcc $(CFLAGS) -o server threaded-server.c $(LIBS)

run: build
        @./server -h

benchmark: build
        @nohup ./server 8080 $(shell pwd) >/dev/null 2>&1 &
        @ab -n 1000 -c 100 http://127.0.0.1:8080/server
        @killall -9 server

clean:
        rm -f $(SERVER)
