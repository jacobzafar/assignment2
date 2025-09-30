CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -O2

all: tcpServer udpServer

tcpServer: tcpServer.o common.o
→$(CC) $(CFLAGS) -o tcpServer tcpServer.o common.o

udpServer: udpServer.o common.o
→$(CC) $(CFLAGS) -o udpServer udpServer.o common.o

tcpServer.o: tcpServer.c common.h
→$(CC) $(CFLAGS) -c tcpServer.c

udpServer.o: udpServer.c common.h
→$(CC) $(CFLAGS) -c udpServer.c

common.o: common.c common.h
→$(CC) $(CFLAGS) -c common.c

clean:
→rm -f *.o tcpServer udpServer
