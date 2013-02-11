.PHONY: help run build benchmark clean

help:
	@echo "syntax: make (build|run|clean)"

CFLAGS=-Wall -Werror -I.
LIBS=-lpthread -L. -lmytime -lmyutils
CC=gcc
#CC=clang
S=

indent:
	$(S)indent -linux -nut -ts4 -l1024 threaded-server.c

libs:
	$(S)gcc -D_SHLIB_ $(CFLAGS) -fpic -shared -o libmytime.so my-time.c
	$(S)gcc -D_SHLIB_ $(CFLAGS) -fpic -shared -o libmyutils.so my-utils.c

build: libs
	$(S)gcc $(CFLAGS) -D_GNU_SOURCE -o server threaded-server.c $(LIBS)

run: build
	$(S)env LD_LIBRARY_PATH=$(shell pwd) ./server -p 8080 -r $(shell pwd)/www -i index.html

benchmark: build
	$(S)nohup env LD_LIBRARY_PATH=$(shell pwd) ./server -p 8080 -r $(shell pwd) -i index.html >/dev/null 2>&1 &
	$(S)ab -t 300 -c 100 -H 'Host: localhost' http://127.0.0.1:8080/server
	$(S)killall -9 server

benchmark-head: build
	$(S)nohup env LD_LIBRARY_PATH=$(shell pwd) ./server -p 8080 -r $(shell pwd) -i index.html >/dev/null 2>&1 &
	$(S)ab -i -t 300 -c 100 -H 'Host: localhost' http://127.0.0.1:8080/server
	$(S)killall -9 server

clean:
	rm -f server a.out *.so
