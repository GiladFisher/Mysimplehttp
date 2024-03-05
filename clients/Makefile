#flags 

all: client async_client

client: client.c
	gcc -o client client.c

async_client: async_client.c
	gcc -o async_client async_client.c -lpthread

clean:
	rm -f client async_client *.o

phony: all clean

