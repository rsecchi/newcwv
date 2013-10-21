

CFLAGS = -O2 -g -Wall
LIBS = -lpthread

all: server client

server: tmix_server.c
	gcc tmix_server.c -o $@ ${LIBS} ${CFLAGS}


client: tmix_client.c
	gcc tmix_client.c -o $@ ${LIBS} ${CFLAGS}


clean:
	rm -rf server client

