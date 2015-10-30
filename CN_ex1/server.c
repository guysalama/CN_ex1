#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> // for open flags
#include <time.h> // for time measurement
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <math.h>
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

int MSG_SIZE = 50;
int HEAPS_NUM = 3;

struct gameData{
	int valid;
	int win;
	int heaps[3];
};

struct move{
	int heap;
	int amount;
};


void checkForNegativeValue(int num, char* func, int sock);
void checkForZeroValue(int num, int sock);
int myBind(int sock, const struct sockaddr_in *myaddr, int size);
int IsBoardClear(struct gameData game);
void RemoveOnePieceFromBiggestHeap(struct gameData * game);
int MaxNumIndex(int heaps[HEAPS_NUM]);
void CheckAndMakeClientMove(struct gameData * game, struct move clientMove);
int sendAll(int s, char *buf, int *len);
int receiveAll(int s, char *buf, int *len);

int main(int argc, char** argv){
	int sock, errorIndicator;
	struct sockaddr_in myaddr;
	struct sockaddr addrBind;
	struct in_addr inAddr;
	char buf[MSG_SIZE];

	int i, port;
	struct gameData game;
	struct move clientMove;

	// Region input Check
#if (1)
	if (argc<4 || argc>5){
		printf("Illegal arguments\n");
		exit(1);
	}

	for (i = 0; i<HEAPS_NUM; i++){
		sscanf(argv[i + 1], "%d", &game.heaps[i]);
		if (game.heaps[i]<1 || game.heaps[i]>1000){
			printf("Illegal arguments. heap sizes should between 1 to 1000\n");
			exit(1);
		}
	}

	if (argc == 5) sscanf(argv[4], "%d", &port);
	else port = 6444;

#endif

	sock = socket(AF_INET, SOCK_STREAM, 0);
	checkForNegativeValue(sock, "socket", sock);
	addrBind.sa_family = AF_INET;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(port);
	inAddr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_addr = inAddr;
	errorIndicator = myBind(sock, &myaddr, sizeof(addrBind));
	checkForNegativeValue(errorIndicator, "bind", sock);
	errorIndicator = listen(sock, 5);
	checkForNegativeValue(errorIndicator, "listen", sock);
	sock = accept(sock, (struct sockaddr*)NULL, NULL);
	checkForNegativeValue(sock, "accept", sock);

	game.valid = 1;
	game.win = -1;

	sprintf(buf, "%d$%d$%d$%d$%d", game.valid, game.win, game.heaps[0], game.heaps[1], game.heaps[2]);
	while (1){
		errorIndicator = sendAll(sock, buf, &MSG_SIZE);
		checkForNegativeValue(errorIndicator, "send", sock);

		if (game.win != -1) // If the game is over the server disconnect
		{
			close(sock);
			exit(0);
		}

		errorIndicator = receiveAll(sock, buf, &MSG_SIZE);
		checkForNegativeValue(errorIndicator, "recv", sock);
		sscanf(buf, "%d$%d", &clientMove.heap, &clientMove.amount);
		CheckAndMakeClientMove(&game, clientMove);

		if (IsBoardClear(game)) game.win = 1; // Client win
		else{
			RemoveOnePieceFromBiggestHeap(&game);
			if (IsBoardClear(game)) game.win = 2; // server win
		}

		sprintf(buf, "%d$%d$%d$%d$%d", game.valid, game.win, game.heaps[0], game.heaps[1], game.heaps[2]);
	}
}

void CheckAndMakeClientMove(struct gameData * game, struct move clientMove){
	if (clientMove.heap<0 || clientMove.heap>HEAPS_NUM || clientMove.amount<1){
		game->valid = 0;
		return;
	}
	if (game->heaps[clientMove.heap]<clientMove.amount){
		game->valid = 0;
	}
	else {
		game->valid = 1;
		game->heaps[clientMove.heap] -= clientMove.amount;
	}
}

void checkForNegativeValue(int num, char* func, int sock){
	if (num<0){
		printf("Error: %s\n", strerror(errno));
		close(sock);
		exit(1);
	}
}

void checkForZeroValue(int num, int sock){
	if (num == 0){
		printf("Disconnected from Client\n");
		close(sock);
		exit(1);
	}
}

int myBind(int sock, const struct sockaddr_in *myaddr, int size){
	return bind(sock, (struct sockaddr*)myaddr, sizeof(struct sockaddr_in));
}

void RemoveOnePieceFromBiggestHeap(struct gameData* game){
	int maxHeapIndex = MaxNumIndex(game->heaps);
	game->heaps[maxHeapIndex] -= 1;
	return;
}

int MaxNumIndex(int heaps[HEAPS_NUM]){ //returns the first index with the highest number
	int i, biggestIndex = 0;
	for (i = 1; i<HEAPS_NUM; i++){
		if (heaps[biggestIndex]<heaps[i]){
			biggestIndex = i;
		}
	}
	return biggestIndex;
}

int IsBoardClear(struct gameData game){
	int i;
	for (i = 0; i<HEAPS_NUM; i++){
		if (game.heaps[i]>0){
			return 0;
		}
	}
	return 1;
}

int sendAll(int s, char *buf, int *len) {
	int total = 0; // how many bytes we've sent
	int bytesleft = *len; // how many we have left to send 
	int n;

	while (total < *len) {
		n = send(s, buf + total, bytesleft, 0);
		checkForZeroValue(n, s);
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
		checkForZeroValue(n, s);
		if (n == -1) { break; }
		total += n;
		bytesleft -= n;
	}
	*len = total; // return number actually sent here
	return n == -1 ? -1 : 0; // -1 on failure, 0 on success
}

