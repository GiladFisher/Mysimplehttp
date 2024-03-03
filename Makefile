#flags 

all: server client async_client

server: server.c
	gcc -o server server.c -lpthread

client: client.c
	gcc -o client client.c

async_client: async_client.c
	gcc -o async_client async_client.c -lpthread

clean:
	rm -f server client async_client *.o

phony: all clean

