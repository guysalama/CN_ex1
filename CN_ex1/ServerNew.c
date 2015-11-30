
/*
* Server.c
*
*  Created on: Nov 25, 2015
*      Author: dorbank
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h> // for open flags
#include <time.h> // for time measurement
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// Messages
#define ILLEGAL_ARGS "Illegal arguments\n"
#define SOCKET_ERROR "Error opening the socket: %s\n"
#define YOUR_TURN "your turn:\n"
#define ILLEGAL_INPUT "Illegal input! Game over"
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
#define DEFAULT_PORT 6444
#define HEAPS_NUM 3
#define BUF_SIZE 300
#define MSGTXT_SIZE 255


struct gameData{
	int valid;
	int msg; // 1/2 - message by that player . 0 - move
	int isMyTurn; // 0 - no, 1 - yes
	int win; //-1 game on, <player id> - the player id who won, 2 - disconnected
	int myPlayerId; // player id (0/1)
	int LastTurnHeap; //which heap was removed from. -1 means it was illegal move
	int LastTurnRemoves; //amount removed from that heap
	int heaps[3];
	char msgTxt[255];
};

//struct gameData{
//	int valid;
//	int msg; // <sender Id> - this is a message, (-1) - send to all - (0) this is not a msg
//	int isMyTurn; // 0 - no, 1 - yes
//	int win; // 0 - no one, <player id> - the player id who won
//	int numOfPlayers; // p - the number of players the server allows to play
//	int myPlayerId; // player id (0 - p-1), if i dont play: -1
//	int playing; // 0 - viewing, 1 - playing
//	int isMisere;
//	int heapA;
//	int heapB;
//	int heapC;
//	int moveCount; // amount of moves that were made
//	char msgTxt[100]; // TODO: unknown error with 'MSGTXT_SIZE - 100' while compiling
//};

struct clientMsg{
	int heap;
	int removes;
	int msg; // 1 - this is a message, 0 - this is a move
	char msgTxt[255];
};

//struct clientMsg{
//	int heap;
//	int amount;
//	int msg; // 1 - this is a message, 0 - this is a move
//	int recp; // player id to send the message to (0 - p-1)
//	int moveCount; // amount of move that were made
//	char msgTxt[100]; // TODO: unknown error with 'MSGTXT_SIZE - 100' while compiling
//};

struct clientData{
	int fd;			// fd that was returned from select
	//int clientNum;	// client number implemented bonus style
	//int isPlayer;
	char readBuf[BUF_SIZE];		// contains data read from client
	char writeBuf[BUF_SIZE]; 	// contains data to write to client
	//int amountToWrite; // indicating amount of data contains by writeBuf
};

struct clientData ClientsQueue[2];	// queue for connected clients
int minFreeClientNum = 1;			// lowest available client number
int clientIndexTurn = 0;			// current turn of client (index according to queue)
int conPlayers = 0;					// amount of connected players
struct gameData game;				// global game struct
int LastTurnHeap = 0;
int LastTurnRemoves = 0;

// game utils
int myBind(int sock, const struct sockaddr_in *myaddr, int size);
int IsBoardClear(struct gameData game);
//void RemoveOnePieceFromBiggestHeap(struct gameData * game);
//int MaxNum(int a, int b, int c, int d);
int CheckAndMakeClientMove(struct clientMsg clientMove);

// common
int sendAll(int s, char *buf, int *len);
void checkForZeroValue(int num, int sock);
void checkForNegativeValue(int num, char* func, int sock);
int parseClientMsg(char buf[MSGTXT_SIZE], struct clientMsg *data);
void createClientMsgBuff(struct clientMsg data, char* buf);
void createGameDataBuff(struct gameData data, char* buf);
int parseGameData(char buf[MSGTXT_SIZE], struct gameData* data);

void PRINT_Debug(char* msg);
void SendCantConnectToClient(int fd);
void sendInvalidMoveToPlayer(int index);
void updateEveryoneOnMove(int index);
//void updateEveryoneOnMoveExceptIndex(int index);
void notifyOnTurn();
//void notifyOnWinningToPlayer(int index);
void notifyOnDisconnectionToPlayer(int index);
void notifyOnWinningToAll(int index);
void addClientToQueue(int newFd);
//int notifyOnWinningToPlayer(int idx);
void handleMsg(struct clientMsg, int index);
void handleIncomingMsg(struct clientMsg data, int index);
void handleReadBuf(int index);
int receiveFromClient(int index);
int sendToClient(int index);


int main(int argc, char** argv){
	int sockListen, errorIndicator;
	int maxClientFd;
	int i, j;
	struct sockaddr_in myaddr;
	struct sockaddr addrBind;
	struct in_addr inAddr;
	fd_set fdSetRead, fdSetWrite;
	//struct timeval timeout = { 60, 0 };

	int h;
	int port;

	if (argc < 4 || argc > 5){
		printf(ILLEGAL_ARGS);
		exit(1);
	}

	for (j = 1; j < 4; j++){
		sscanf(argv[j], "%d", &h);
		game.heaps[j - 1] = h;
	}

	if (argc == 5) sscanf(argv[4], "%d", &port);
	else port = DEFAULT_PORT;

	game.valid = 1;
	game.win = -1;
	//game.numOfPlayers = p;
	game.msg = 0;


	// Set listner. accepting only in main loop
	sockListen = socket(AF_INET, SOCK_STREAM, 0);
	checkForNegativeValue(sockListen, "socket", sockListen);
	printf("Succesfully got a socket number: %d\n", sockListen);
	addrBind.sa_family = AF_INET;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(port);
	inAddr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_addr = inAddr;
	errorIndicator = myBind(sockListen, &myaddr, sizeof(addrBind));
	checkForNegativeValue(errorIndicator, "bind", sockListen);
	printf("Succesfully binded %d\n", sockListen);
	errorIndicator = listen(sockListen, 2);
	checkForNegativeValue(errorIndicator, "listen", sockListen);
	printf("Succesfully started listening: %d\n", sockListen);

	while (1){
		// clear set and add listner
		maxClientFd = sockListen;
		FD_ZERO(&fdSetRead);
		FD_ZERO(&fdSetWrite);
		FD_SET(sockListen, &fdSetRead);

		// add all clients to fdSetRead
		for (i = 0; i < conPlayers; i++){
			printf("Adding fd:%d to read\n", ClientsQueue[i].fd);
			FD_SET(ClientsQueue[i].fd, &fdSetRead);
			if (strlen(ClientsQueue[i].writeBuf) > 0){
				printf("Adding fd:%d to write\n", ClientsQueue[i].fd);
				FD_SET(ClientsQueue[i].fd, &fdSetWrite);
			}
			else{
				printf("ClientsQueue[i].writeBuf = %s\n", ClientsQueue[i].writeBuf);
			}
			if (ClientsQueue[i].fd > maxClientFd) maxClientFd = ClientsQueue[i].fd;
		}

		// TODO: need to add timeout
		printf("Select...\n");
		select(maxClientFd + 1, &fdSetRead, &fdSetWrite, NULL, NULL);
		printf("Exit select...fdReady\n");// = %d\n",fdready);
		if (FD_ISSET(sockListen, &fdSetRead)){
			printf("Reading from sock listen\n");
			int fdCurr = accept(sockListen, (struct sockaddr*)NULL, NULL);
			checkForNegativeValue(fdCurr, "accept", fdCurr);
			if (fdCurr >= 0){
				printf("Got a valid FD after accept, fd:%d\n", fdCurr);
				if ((conPlayers) == 2) SendCantConnectToClient(fdCurr);
				else{
					printf("new client is a player: fd:%d\n", fdCurr);
					addClientToQueue(fdCurr);
					if ((conPlayers == 2)){
						notifyOnTurn();
					}
				}
			}
		}

		// Service all the sockets with input pending.
		for (i = 0; i < conPlayers; ++i){
			if (FD_ISSET(ClientsQueue[i].fd, &fdSetRead)){
				errorIndicator = receiveFromClient(i);
				if (errorIndicator < 0){
					close(ClientsQueue[i].fd);
					notifyOnDisconnectionToPlayer(i);
				}
				else if (errorIndicator == 1){
					handleReadBuf(i);
				}

			}

			if (FD_ISSET(ClientsQueue[i].fd, &fdSetWrite)){
				printf("sending for fd %d\n", ClientsQueue[i].fd);
				errorIndicator = sendToClient(i);
				if (errorIndicator < 0){
					close(ClientsQueue[i].fd);
					notifyOnDisconnectionToPlayer(i);
				}
			}
		}
	}
}

int sendToClient(int index){
	int n;

	if (strlen(ClientsQueue[index].writeBuf) == 0){
		//nothing to send
		return 0;
	}
	printf("sending packet %s\n", ClientsQueue[index].writeBuf);
	n = send(ClientsQueue[index].fd, ClientsQueue[index].writeBuf, strlen(ClientsQueue[index].writeBuf), 0);
	if (n <= 0){
		//client disconected
		return -1;
	}
	strcpy(ClientsQueue[index].writeBuf, ClientsQueue[index].writeBuf + n);
	printf("sent packet to fd: %d. buf is %s\n", ClientsQueue[index].fd, ClientsQueue[index].writeBuf);//TODO
	return 1;
}


//action for clientQueue[index]

void handleReadBuf(int index){
	struct clientMsg data;
	int retVal;
	parseClientMsg(ClientsQueue[index].readBuf, &data);

	if (data.msg != 0) handleIncomingMsg(data, index);
	else{
		// client sent a move
		if (index != clientIndexTurn){
			// it is not the client turn, if didnt catch on the client side
			sendInvalidMoveToPlayer(index);
			return;
		}

		retVal = CheckAndMakeClientMove(data);
		clientIndexTurn = (clientIndexTurn + 1) % (conPlayers); // keep the turn moving only between connected players
		if (retVal == -1){
			sendInvalidMoveToPlayer(index);
			LastTurnHeap = -1;
			//updateEveryoneOnMoveExceptIndex(index);
			notifyOnTurn();
		}
		else if (retVal == 1) notifyOnWinningToAll(index);
		else notifyOnTurn();
		//if (retVal == 0) notifyOnTurn();
		//	if (retVal == 0) {
		//		notifyOnTurn();
		//	}
		//}
	}

	// deleting read data from readBuf
	int i;
	for (i = 0; i < MSGTXT_SIZE; ++i)
	{
		if (ClientsQueue[index].readBuf[i] == '}')
		{
			ClientsQueue[index].readBuf[i] = '\0';
			if (ClientsQueue[index].readBuf[i + 1] == '{')
			{
				//printf("in the if\n");
				strcpy(ClientsQueue[index].readBuf, ClientsQueue[index].readBuf + i + 1);
				break;
			}
		}
		ClientsQueue[index].readBuf[i] = '\0';
	}
}

void notifyOnTurn(){
	char buf[MSGTXT_SIZE];
	struct gameData newGame;
	int i;

	newGame.isMyTurn = 1;
	newGame.valid = 1;
	//newGame.playing = 1;
	newGame.msg = 0;
	newGame.win = game.win;
	newGame.myPlayerId = clientIndexTurn;
	for (i = 0; i < HEAPS_NUM; i++) newGame.heaps[i] = game.heaps[i];

	createGameDataBuff(newGame, buf);
	strcpy(ClientsQueue[clientIndexTurn].writeBuf, buf);
	//strcat(ClientsQueue[clientIndexTurn].writeBuf, buf);
}

void notifyOnDisconnectionToPlayer(int index){
	char buf[MSGTXT_SIZE];
	//int index;

	//index = conPlayers;
	//ClientsQueue[index].isPlayer = 1;

	game.isMyTurn = 0;
	game.valid = 1;
	game.win = 2; // (index == 0) ? 2 : 1;

	createGameDataBuff(game, buf);
	strcat(ClientsQueue[1 - index].writeBuf, buf);
}

void notifyOnWinningToAll(int index){
	int i;
	char buf[MSGTXT_SIZE];

	game.myPlayerId = index; // ClientsQueue[index].clientNum;
	game.win = index + 1;

	for (i = 0; i < conPlayers; i++){
		createGameDataBuff(game, buf);
		strcat(ClientsQueue[i].writeBuf, buf);
	}
	//	game.playing = ClientsQueue[i].isPlayer;
	//	createGameDataBuff(game, buf);
	//	strcat(ClientsQueue[i].writeBuf, buf);
	//}
}

void updateEveryoneOnMove(int index){
	//int i;
	char buf[MSGTXT_SIZE];

	game.myPlayerId = index; // ClientsQueue[index].clientNum;??????? TODO
	createGameDataBuff(game, buf);
	strcpy(ClientsQueue[1 - index].writeBuf, buf);
	//strcat(ClientsQueue[1-index].writeBuf, buf);

	//for (i = 0; i<conPlayers + conViewers; i++){
	//	game.playing = ClientsQueue[i].isPlayer;
	//	createGameDataBuff(game, buf);
	//	strcat(ClientsQueue[i].writeBuf, buf);
	//}
}

void sendInvalidMoveToPlayer(int index){
	char buf[MSGTXT_SIZE];
	game.valid = 0;

	strcpy(game.msgTxt, "\0");
	createGameDataBuff(game, buf);

	// restore value
	game.valid = 1;
	strcat(ClientsQueue[index].writeBuf, buf);
}

//handles msg written by client clientQueue[index]
void handleIncomingMsg(struct clientMsg data, int index){
	//int i;
	char buf[MSGTXT_SIZE];
	struct gameData newGame;

	newGame.valid = 1;
	newGame.msg = 1; // 1/2 - message by that player . 0 - move //index + 1;

	strncpy(newGame.msgTxt, data.msgTxt, strlen(data.msgTxt));
	newGame.msgTxt[strlen(data.msgTxt)] = '\0';

	createGameDataBuff(newGame, buf);
	index == 0 ? strcat(ClientsQueue[1].writeBuf, buf) : strcat(ClientsQueue[0].writeBuf, buf);
}

/**
handles receive from client.
1 for read all data from client
0 for read partial data
-1 for disconnected client
*/
int receiveFromClient(int index){
	int n;
	int len = strlen(ClientsQueue[index].readBuf);
	if (strlen(ClientsQueue[index].readBuf) < 10) len = 0;
	n = recv(ClientsQueue[index].fd, ClientsQueue[index].readBuf + len, MSGTXT_SIZE * 5, 0);

	if (n <= 0) return -1; //client disconected
	const char* startPtr = strchr(ClientsQueue[index].readBuf, '{');
	const char* endPtr = strchr(ClientsQueue[index].readBuf, '}');
	if (startPtr && endPtr) return 1;
	return 0;
}

//void sendClientConnected(int fd, struct gameData *data){
//	struct clientData thisClientData;
//	char buf[MSGTXT_SIZE];
//
//	// last one added
//	thisClientData = ClientsQueue[conPlayers];
//
//	data->valid = 1;
//	data->msg = 0;
//	data->myPlayerId = thisClientData.clientNum;
//	data->playing = thisClientData.isPlayer;
//
//	parseGameData(buf, data);
//	int errorIndicator = sendAll(fd, buf, &MSGTXT_SIZE);
//	checkForNegativeValue(errorIndicator, "send", fd);
//
//}

void SendCantConnectToClient(int fd){
	int errorIndicator;
	char buf[MSGTXT_SIZE];
	struct gameData newGame;
	int msgtxt_size = MSGTXT_SIZE;
	// -1 stands for too many clients connected
	newGame.valid = 0;
	createGameDataBuff(newGame, buf);

	errorIndicator = sendAll(fd, buf, &msgtxt_size);
	checkForNegativeValue(errorIndicator, "send", fd);

	close(fd);
}

/*
returns:	-1 invalid move
1 valid move, client won,
0 valid move, nobody won
**/
int CheckAndMakeClientMove(struct clientMsg clientMove){
	//printf("checking move for heap:%d, count:%d\n",clientMove.heap, clientMove.amount);
	// move count is allways increased if a player played on his turn
	//game.moveCount++;
	if (clientMove.heap<0 || clientMove.heap>(HEAPS_NUM - 1)){
		return -1;
	}
	if (game.heaps[clientMove.heap] < clientMove.removes) return -1;
	else{
		game.valid = 1;
		game.heaps[clientMove.heap] -= clientMove.removes;
	}

	if (IsBoardClear(game)){
		game.win = clientIndexTurn; //ClientsQueue[clientIndexTurn].clientNum;
		return 1;
	}
	return 0;
}

void checkForNegativeValue(int num, char* func, int sock){
	if (num<0){
		close(sock);
		printf("error occured while %s op", func);
	}
}

int myBind(int sock, const struct sockaddr_in *myaddr, int size){
	return bind(sock, (struct sockaddr*)myaddr, sizeof(struct sockaddr_in));
}

int IsBoardClear(struct gameData game){
	int j;
	for (j = 0; j < HEAPS_NUM; j++){
		if (game.heaps[j] != 0) return 0;
	}
	return 1;
}


int sendAll(int s, char *buf, int *len) {
	int total = 0; /* how many bytes we've sent */
	int bytesleft = *len; /* how many we have left to send */
	int n;

	while (total < *len) {
		n = send(s, buf + total, bytesleft, 0);
		checkForZeroValue(n, s);
		if (n == -1) { break; }
		total += n;
		bytesleft -= n;
		printf("sent [total %d , bytesleft %d], exactly %s", total, bytesleft, buf);
	}
	*len = total; /* return number actually sent here */

	return n == -1 ? -1 : 0; /*-1 on failure, 0 on success */
}

void checkForZeroValue(int num, int sock){
	if (num == 0){
		close(sock);
		exit(1);
	}
}

/**
newFd - new client's fd returned from select
isPlayer - 1 for player, 0 for viewer
*/
void addClientToQueue(int newFd){ //int isPlayer){
	struct clientData newClient;
	int newClientIndex;
	struct gameData newGame;
	int i;

	// handling queue
	newClientIndex = conPlayers;
	newClient.fd = newFd;
	ClientsQueue[newClientIndex] = newClient;
	conPlayers++;

	// handling writeBuf
	newGame.valid = 1;
	newGame.msg = 0;
	newGame.isMyTurn = (conPlayers == 1) ? 1 : 0; // this is new client turn only if he is the only one here
	newGame.win = -1;
	newGame.myPlayerId = newClientIndex;
	for (i = 0; i < HEAPS_NUM; i++){
		newGame.heaps[i] = game.heaps[i];
	}

	// first write to writeBuf. no worries of ruining previous data
	createGameDataBuff(newGame, ClientsQueue[newClientIndex].writeBuf);
	printf("end of addition to queue\n");//TODO
}

/**
clientMove - struct containing msgTxt and reciver
index - ClientsQueue index of sender
*/
void handleMsg(struct clientMsg clientMove, int index){
	struct gameData data;
	//int i;
	char buf[MSGTXT_SIZE];
	int msg_size = MSGTXT_SIZE;

	data.valid = 1;
	data.msg = 1;
	strcpy(data.msgTxt, clientMove.msgTxt);
	createClientMsgBuff(clientMove, buf);
	sendAll(ClientsQueue[1 - index].fd, buf, &msg_size);
}

/////////////////// parsers <-> creates //////////////////////////////

void createClientMsgBuff(struct clientMsg data, char* buf){
	if (data.msg == 0)
	{
		data.msgTxt[0] = 'a';
		data.msgTxt[1] = '\0';
	}
	sprintf(buf, "{%d$%d$%d$%s}",
		data.heap,
		data.removes,
		data.msg,
		//data.recp,
		//data.moveCount,
		data.msgTxt);
	//printf("successfully created msg %s\n",buf);
}

int parseClientMsg(char buf[MSGTXT_SIZE], struct clientMsg *data){
	int x = sscanf(buf, "{%d$%d$%d$%[^}]",
		&data->heap,
		&data->removes,
		&data->msg,
		&data->msgTxt[0]);
	LastTurnHeap = data->heap;
	LastTurnRemoves = data->removes;
	//&data->recp,
	//&data->moveCount,
	//printf("successfully parsed msg %s\n",buf);
	return x;
}

int parseGameData(char buf[MSGTXT_SIZE], struct gameData* data){
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
	if (data.msg == 0)
	{
		data.msgTxt[0] = 'a';
		data.msgTxt[1] = '\0';
	}
	sprintf(buf, "{%d$%d$%d$%d$%d$%d$%d$%d$%d$%d$%s}",
		data.valid,
		data.isMyTurn,
		data.msg,
		data.win,
		data.myPlayerId,
		LastTurnHeap,
		LastTurnRemoves,
		data.heaps[0],
		data.heaps[1],
		data.heaps[2],
		data.msgTxt);
	//printf("data buf created: %s\n",buf);//TODO
}
