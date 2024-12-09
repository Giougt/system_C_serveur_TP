CC=gcc
CFLAGS=-Wall -Werror

binhttpd: binhttpd.c
    $(CC) $(CFLAGS) -o binhttpd binhttpd.c
