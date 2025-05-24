# Minimal Makefile for Unix3 IRC Chatbot
CC=gcc
CFLAGS=-Wall -g
SRC=src/main.c src/config.c src/irc_client.c src/narrative.c src/shared_mem.c src/admin.c src/utils.c
OBJ=$(SRC:.c=.o)

all: irc_bot

irc_bot: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f irc_bot *.o src/*.o
