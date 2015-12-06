#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> // for open flags
#include <time.h> // for time measurement
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

// Messages
#define ILLEGAL_ARGS "Illegal arguments\n"
#define SOCKET_ERROR "Error opening the socket: %s\n"
#define YOUR_TURN "your turn:\n"
#define ILLEGAL_INPUT "Illegal input! Game over\n"
#define CONNECTION_ERROR "failed to connect\n"
#define RECEIVE_ERROR "failed to receive data from server\n"
#define LEGAL_MOVE "Move accepted\n"
#define ILLEGAL_MOVE "Illegal move\n"
#define GAME_OVER "Game over!\n"
#define CLIENT_WIN "You win!\n"
#define CLIENT_LOSE "You lose!\n"
#define CONNECTION_REJECTION "Client rejected\n"
#define MOVE_REJECTED "Move rejected: this is not your turn. please wait\n"

// Constants
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT "6444"
#define HEAPS_NUM 3
#define BUF_SIZE 300
#define MSGTXT_SIZE 200
#define STDIN 0

// Structs
struct gameData{
	int valid;
	int msg; // 1/2 - message by that player . 0 - move
	int isMyTurn; // 0 - no, 1 - yes
	int win; //-1 game on, <player id> - the player id who won, 2 - other player disconnected
	int myPlayerId; // player id (0/1)
	int LastTurnHeap; //which heap was removed from.
	int LastTurnRemoves; //how many cubes removed from that heap
	int heaps[3]; //the status of each heap
	char msgTxt[200]; //free text space
};

struct clientMsg{
	int heap; //which heap to remove from
	int removes; //how many cubes to remove
	int msg; // 1 - this is a message, 0 - this is a move
	char msgTxt[200]; //free text space
};

// Globals
int playerId, myTurn;
struct gameData game;
struct clientMsg cmQueue[5];
int cmQueueLength = 0;

int connectToServer(int sock, const char* address, char* port);
void printGameState(struct gameData game);
void printWinner(struct gameData game);
struct clientMsg getMoveFromInput(int sock, char* cmd);
int send_all(int s, char *buf, int *len);
int receive_all(int s, char *buf, int *len, int first);
void checkForZeroValue(int num, int sock);
void opCheck(int num, char* func, int sock);
int parseClientMsg(char buf[BUF_SIZE], struct clientMsg *data);
void createClientMsgBuff(struct clientMsg data, char* buf);
void createGameDataBuff(struct gameData data, char* buf);
int parseGameData(char buf[BUF_SIZE], struct gameData* data);
void handleMsg(char *buf);
void handleFirstMsg(char *buf);
int opponentId();

int main(int argc, char const *argv[]){
	char port[20];
	char address[50];
	int i, j;

	if (argc<1 || argc>3){
		printf(ILLEGAL_ARGS);
		exit(1);
	}
	if (argc == 1 || argc == 2){
		strcpy(port, DEFAULT_PORT);
		if (argc == 1) strcpy(address, DEFAULT_HOST);
	}
	if (argc == 2 || argc == 3){
		strcpy(address, argv[1]);
		if (argc == 3){
			strcpy(port, argv[2]);
		}
	}
	int sock = socket(AF_INET, SOCK_STREAM, 0); // Get socket
	if (sock == -1){
		printf(SOCKET_ERROR, strerror(errno));
		return errno;
	}
	// Connect to server
	sock = connectToServer(sock, address, port);
	// Get initial data
	char readBuf[BUF_SIZE];
	int bufSize = BUF_SIZE;
	receive_all(sock, readBuf, &bufSize, 1);
	// Get the initial data from the server
	if (game.valid == 0){
		printf(CONNECTION_REJECTION);
		return 0;
	}

	playerId = game.myPlayerId;
	myTurn = game.isMyTurn;
	printf("You are client %d\n", playerId + 1);
	if (playerId == 0){
		printf("Waiting to client 2 to connect.\n");
		receive_all(sock, readBuf, &bufSize, 1); //wait until second player connects
	}

	printGameState(game);
	int addReadyForSend = 0;
	fd_set fdSetRead, fdSetWrite;
	struct clientMsg cm;

	if (myTurn == 1){
		printf(YOUR_TURN);
	}
	while (game.win == -1){
		int maxClientFd = sock;

		FD_ZERO(&fdSetRead);
		FD_ZERO(&fdSetWrite);
		FD_SET(STDIN, &fdSetRead);
		FD_SET(sock, &fdSetRead);

		if (addReadyForSend == 1){
			FD_SET(sock, &fdSetWrite);
		}

		int fdReady = select(maxClientFd + 1, &fdSetRead, &fdSetWrite, NULL, NULL);
		if (fdReady == 0){ //chicken check.
			continue;
		}

		if (FD_ISSET(sock, &fdSetWrite) && addReadyForSend == 1){//packets are ready for send
			int buf = BUF_SIZE;
			char cmBuffer[BUF_SIZE];
			i = 0;
			while (i<cmQueueLength){ //send as fifo
				createClientMsgBuff(cmQueue[i], cmBuffer);
				if (send_all(sock, cmBuffer, &buf) == -1){
					break;
				}
				i++;
			}
			j = -1;
			while (i<cmQueueLength){ //reorganize cmQueue
				j++;
				cmQueue[j] = cmQueue[i];
				i++;
			}
			cmQueueLength = j + 1;
			if (cmQueueLength == 0){ addReadyForSend = 0; };
		}
		if (FD_ISSET(STDIN, &fdSetRead)){// there is input from cmd
			int rSize = BUF_SIZE;
			fgets(readBuf, rSize, stdin);
			cm = getMoveFromInput(sock, readBuf);
			if (cm.msg == 1){ //it's a message! send it right away!
				cmQueue[cmQueueLength] = cm;
				cmQueueLength++;
				addReadyForSend = 1;
			}
			else{//it's a turn.
				if (myTurn != 1){// not my turn!
					printf(MOVE_REJECTED);
				}
				else{
					cmQueue[cmQueueLength] = cm;
					cmQueueLength++;
					addReadyForSend = 1;
				}
			}
		}
		if (FD_ISSET(sock, &fdSetRead)){
			char rBuf[BUF_SIZE];
			int rSize = BUF_SIZE;
			receive_all(sock, rBuf, &rSize, 0);
		}
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
			perror("connect");
			continue;
		}
		break; // if we get here, we must have connected successfully
	}
	if (p == NULL) {
		// looped off the end of the list with no connection
		fprintf(stderr, CONNECTION_ERROR);
		close(sock);
		exit(2);
	}
	freeaddrinfo(servinfo); // all done with this structure
	return sock;
}

struct clientMsg getMoveFromInput(int sock, char* cmd){
	int heap, removes;
	char heapC;
	char msg[MSGTXT_SIZE];
	struct clientMsg m;

	// Exit if user put Q
	if (cmd[0] == 'Q'){
		int buf = BUF_SIZE;
		send_all(sock, "", &buf);
		close(sock);
		exit(0);
	}

	if (sscanf(cmd, "MSG %[^\n]", msg) == 1){
		m.msg = 1;
		m.heap = 1;
		m.removes = 1;
		strcpy(m.msgTxt, msg);
		m.msgTxt[strlen(msg)] = '\0';
		return m;
	}

	sscanf(cmd, "%c %d", &heapC, &removes);
	heap = (int)heapC - (int)'A';

	m.heap = heap;
	m.removes = removes;
	m.msg = 0;

	return m;
}
////////////////////////   prints   //////////////////////////////////
void printWinner(struct gameData game){
	if (game.win == playerId){
		printf(CLIENT_WIN);
	}
	else if (game.win == 2){
		printf("Client %d was disconnected from the server\n", opponentId() + 1);
		printf(CLIENT_WIN);
	}
	else {
		printf(CLIENT_LOSE);
	}
}

void printGameState(struct gameData game){
	printf("Heap A: %d\n", game.heaps[0]);
	printf("Heap B: %d\n", game.heaps[1]);
	printf("Heap C: %d\n", game.heaps[2]);
}
///////////////////////  send/receive  //////////////////////
int send_all(int s, char *buf, int *len) {
	int total = 0; // how many bytes we've sent
	int bytesleft = *len; // how many we have left to send
	int n;
	while (total < *len) {
		n = send(s, buf + total, bytesleft, 0);
		if (n == -1) break;
		total += n;
		bytesleft -= n;
	}
	*len = total; // return number actually sent here
	return n == -1 ? -1 : 0; // -1 on failure, 0 on success
}

int receive_all(int s, char *buf, int *len, int first) {
	int total = 0; // how many bytes we've received
	size_t bytesleft = *len; // how many we have left to receive
	int n;
	int endOfMsg = 0;
	int index = 0;
	int beforeFirst = 1;
	while (endOfMsg == 0) {//case we get bad packet, we check manually.
		char rBuff[400];
		if (beforeFirst == 0){
			n = recv(s, rBuff + total, 400, 0);
			strcat(buf, rBuff);
		}
		else{
			n = recv(s, buf + total, bytesleft, 0);
			beforeFirst = 0;
		}
		//checkForZeroValue(n,s);
		if (n == -1) { break; }
		total += n;
		bytesleft -= n;
		const char *ptr = strchr(buf, '}');
		while (ptr) {
			index = ptr - buf + 1;
			// Handle msg!!!
			if (first == 1){
				endOfMsg = 1;
				handleFirstMsg(buf);
				ptr = NULL;
				continue;
			}
			char currBuf[BUF_SIZE];
			strcpy(currBuf, buf);
			currBuf[index] = '\0';
			handleMsg(currBuf);
			strcpy(buf, currBuf);
			endOfMsg = 1;
			break;
		}
		if (first == 1){
			break;
		}
	}
	*len = total; // return number actually sent here
	return n == -1 ? -1 : 0; //-1 on failure, 0 on success
}
////////////////////// message handlers /////////////////////////////
void handleFirstMsg(char *buf){
	parseGameData(buf, &game);
}

void handleMsg(char *buf){
	int oldMyTurn = game.isMyTurn;
	struct gameData currGame;
	parseGameData(buf, &currGame);
	if (currGame.msg != 0){//it's a message!
		char txt[MSGTXT_SIZE];
		strncpy(txt, currGame.msgTxt, strlen(currGame.msgTxt));
		txt[strlen(currGame.msgTxt)] = '\0';
		printf("Client %d: %s\n", opponentId() + 1, txt);
		return;
	}
	//it's a turn!
	parseGameData(buf, &game);
	myTurn = game.isMyTurn;
	if (oldMyTurn == 1 && myTurn == 0){//turn is changed. I must have sent a move!
		if (game.valid == 1){
			printf(LEGAL_MOVE);
		}
		else{
			printf(ILLEGAL_MOVE);
		}
	}
	if (oldMyTurn == 0 && myTurn == 1){
		if (game.LastTurnHeap == -1){
			printf("Client %d made an illegal move\n", opponentId() + 1);
		}
		else {
			printf("Client %d takes %d cubes from Heap %c\n", opponentId() + 1, game.LastTurnRemoves, (char)(game.LastTurnHeap + (int)'A'));
		}
	}
	if (!(oldMyTurn == 0 && myTurn == 1 && game.LastTurnHeap == -1)){
		printGameState(game);
	}
	if (myTurn == 1 && game.win == -1){
		printf(YOUR_TURN);
	}
}
//////////////////// value checks //////////////////////////////////
void checkForZeroValue(int num, int sock){
	if (num == 0){
		printf("Disconnected from server\n");
		close(sock);
		exit(1);
	}
}

void opCheck(int num, char* func, int sock){
	if (num<0){
		printf("Error: %s\n", strerror(errno));
		close(sock);
		exit(1);
	}
}
/////////////////// parsers <-> creates //////////////////////////////
void createClientMsgBuff(struct clientMsg data, char* buf){
	if (data.msg == 0){
		data.msgTxt[0] = 'a';
		data.msgTxt[1] = '\0';
	}
	sprintf(buf, "{%d$%d$%d$%s}",
		data.heap,
		data.removes,
		data.msg,
		data.msgTxt);
	//printf("successfully created msg %s\n",buf);
}

int parseClientMsg(char buf[BUF_SIZE], struct clientMsg *data){
	int x = sscanf(buf, "{%d$%d$%d$%[^}]",
		&data->heap,
		&data->removes,
		&data->msg,
		&data->msgTxt[0]);
	//printf("successfully parsed msg %s\n",buf);
	return x;
}

int parseGameData(char buf[BUF_SIZE], struct gameData* data){
	int x = sscanf(buf, "{%d$%d$%d$%d$%d$%d$%d$%d$%d$%d$%[^}]",
		&data->valid,
		&data->isMyTurn,
		&data->msg,
		&data->win,
		&data->myPlayerId,
		&data->LastTurnHeap,
		&data->LastTurnRemoves,
		&data->heaps[0],
		&data->heaps[1],
		&data->heaps[2],
		&data->msgTxt[0]);
	//printf("successfully parsed data %s\n",buf);
	return x;
}

void createGameDataBuff(struct gameData data, char* buf){
	if (data.msg == 0){
		data.msgTxt[0] = 'a';
		data.msgTxt[1] = '\0';
	}
	sprintf(buf, "{%d$%d$%d$%d$%d$%d$%d$%d$%d$%d$%s}",
		data.valid,
		data.isMyTurn,
		data.msg,
		data.win,
		data.myPlayerId,
		data.LastTurnHeap,
		data.LastTurnRemoves,
		data.heaps[0],
		data.heaps[1],
		data.heaps[2],
		data.msgTxt);
	//printf("successfully created data %s\n",buf);
}
int opponentId(){
	return (playerId == 0 ? 1 : 0);
}

