#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> // for open flags
#include <time.h> // for time measurement
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

//Sockets
#include <sys/socket.h>
//Internet addresses
#include <netinet/in.h>
//Working with Internet addresses
#include <arpa/inet.h>
//Domain Name Service (DNS)
#include <netdb.h>
//Working with errno to report errors
#include <errno.h>

struct gameData{
	int valid;
	int win;
	int heaps[3];
};

struct move{
	int heap;
	int amount;
};

int msgSize = 50;
int HEAPS_NUM = 3;
int connectToServer(int sock, const char* address, char* port);
void printGameState(struct gameData game);
void printWinner(struct gameData game);
struct gameData parseDataFromServer(char buf[msgSize]);
struct gameData receiveDataFromServer(int sock);
void printValid(struct gameData game);
struct move getMoveFromInput(int sock);
int sendAll(int s, char *buf, int *len);
int receiveAll(int s, char *buf, int *len);
//void checkForZeroValue(int num, int sock);

int main(int argc, char const *argv[])
{
	char port[20];
	char address[50];

	if (argc<1 || argc>3){
		printf("Illegal arguments\n");
		exit(1);
	}

	if (argc == 1)
	{
		strcpy(address, "localhost");
	}
	else{
		sscanf(argv[1], "%s", &address);
	}

	if (argc < 3)
	{
		strcpy(port, "6444");
	}
	else
	{
		sscanf(argv[2], "%s", &port);
	}

	int sock = socket(AF_INET, SOCK_STREAM, 0); // Get socket
	if (sock == -1)
	{
		printf("Error opening the socket: %s\n", strerror(errno));
		return errno;
	}
	sock = connectToServer(sock, address, port); // Connect to server
	char buf[msgSize];
	struct move Move;
	struct gameData game = receiveDataFromServer(sock); // Get initial data

	printGameState(game);
	while (game.win == -1){
		Move = getMoveFromInput(sock);
		sprintf(buf, "%d$%d", Move.heap, Move.amount);
		if (sendAll(sock, buf, &msgSize) == -1){
			close(sock);
			exit(0);
		}
		game = receiveDataFromServer(sock);
		printValid(game); // Check if move was valid
		printGameState(game); // keep on playing
	}
	printWinner(game);
	return 0;
}

int connectToServer(int sock, const char* address, char* port){
	struct addrinfo hints, *servinfo, *p;
	int rv;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // use AF_INET6 to force IPv6
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(address, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		close(sock);
		exit(1);
	}

	// loop through all the results and connect to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sock = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}
		if (connect(sock, p->ai_addr, p->ai_addrlen) == -1) {
			//close(sock);
			perror("connect");
			continue;
		}
		break; // if we get here, we must have connected successfully
	}

	if (p == NULL) {
		// looped off the end of the list with no connection
		fprintf(stderr, "failed to connect\n");
		close(sock);
		exit(2);
	}

	freeaddrinfo(servinfo); // all done with this structure
	return sock;
}

struct move getMoveFromInput(int sock){
	int heap, amount;
	char heapC;
	char cmd[10];

	printf("Your turn:\n");
	fgets(cmd, 10, stdin);
	if (strcmp(cmd, "Q") == 0) // Exit if user put Q
	{
		close(sock);
		exit(0);
	}
	sscanf(cmd, "%c %d", &heapC, &amount);
	heap = (int) heapC - (int) 'A';
	if (heap < 0 || heap > HEAPS_NUM - 1)
	{
		printf("Illegal input!!!\n");
		close(sock);
		exit(1);
	}
	struct move Move;
	Move.heap = heap;
	Move.amount = amount;
	return Move;
}

struct gameData receiveDataFromServer(int sock)
{
	char buf[msgSize];
	struct gameData game;
	int rec = receiveAll(sock, buf, &msgSize);
	if (rec == -1)
	{
		fprintf(stderr, "failed to receive initial data\n");
		close(sock);
		exit(2);
	}
	game = parseDataFromServer(buf);
	return game;
}

void printValid(struct gameData game)
{
	if (game.valid == 1) printf("Move accepted\n");
	else printf("Illegal move\n");
}

void printWinner(struct gameData game)
{
	if (game.win == 1) printf("You win!\n");
	else if (game.win == 2) printf("Server win!\n");
}

void printGameState(struct gameData game){
	printf("Heap A: %d\n", game.heaps[0]);
	printf("Heap B: %d\n", game.heaps[1]);
	printf("Heap C: %d\n", game.heaps[2]);
}

struct gameData parseDataFromServer(char buf[msgSize]){
	struct gameData game;
	sscanf(buf, "%d$%d$%d$%d$%d", &game.valid, &game.win, &game.heaps[0], &game.heaps[1], &game.heaps[2]);
	return game;
}


int sendAll(int s, char *buf, int *len) {
	int total = 0; // how many bytes we've sent
	int bytesleft = *len; // how many we have left to send 
	int n;
	while (total < *len) {
		n = send(s, buf + total, bytesleft, 0);
		//checkForZeroValue(n,s);
		if (n == -1) { break; }
		total += n;
		bytesleft -= n;
	}
	*len = total; // return number actually sent here 
	return n == -1 ? -1 : 0; // -1 on failure, 0 on success
}

int receiveAll(int s, char *buf, int *len) {
	int total = 0; // how many bytes we've received
	size_t bytesleft = *len; // how many we have left to receive
	int n;

	while (total < *len) {
		n = recv(s, buf + total, bytesleft, 0);
		//checkForZeroValue(n,s);
		if (n == -1) { break; }
		total += n;
		bytesleft -= n;
	}
	*len = total; // return number actually sent here
	return n == -1 ? -1 : 0; // -1 on failure, 0 on success
}

// void checkForZeroValue(int num, int sock){
//	if(num==0){
//		printf( "Disconnected from server\n");
//		close(sock);
//		exit(1);
//	}
//}

