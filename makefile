all: nim-server nim

clean:
	-rm -f Client.o Server.o nim nim-server

nim: Client.o
	gcc Client.o -o nim

nim-server: Server.o
	gcc Server.o -o nim-server

Server.o: Server.c 
	gcc -Wall -g -c Server.c 

Client.o: Client.c
	gcc -Wall -g -c Client.c 
