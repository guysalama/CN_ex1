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
#define DEFAULT_PORT "6444"
#define HEAPS_NUM 3
#define BUF_SIZE 300
#define MSGTXT_SIZE 255


struct gameData{
	int valid;
	int msg; // 1/2 - message by that player . 0 - move
	int isMyTurn; // 0 - no, 1 - yes
	int win; // 0 - no one, <player id> - the player id who won
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
	char readBuf[MSGTXT_SIZE];		// contains data read from client
	char writeBuf[MSGTXT_SIZE]; 	// contains data to write to client
	//int amountToWrite; // indicating amount of data contains by writeBuf 
};

struct clientData ClientsQueue[10];	// queue for connected clients
int minFreeClientNum = 1;			// lowest available client number
int clientIndexTurn = 0;			// current turn of client (index according to queue)
int conPlayers = 0;					// amount of connected players
//int conViewers = 0;					// amount of connected viewers
struct gameData game;				// global game struct

// game utils
int myBind(int sock, const struct sockaddr_in *myaddr, int size);
int IsBoardClear(struct gameData game);
void RemoveOnePieceFromBiggestHeap(struct gameData * game);
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
void updateEveryoneOnMoveExceptIndex(int index);
void notifyOnTurn();
void notifyOnWinningToPlayer(int index);

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
	struct timeval timeout = { 60, 0 };

	int port, h;

	if (argc < 4 || argc > 5){
		printf(ILLEGAL_ARGS);
		exit(1);
	}

	for (j = 1; j < 4; j++){
		sscanf(argv[j], "%d", &h);
		game.heaps[j] = h;
	}
	
	if (argc == 5) sscanf(argv[4], "%d", &port);
	else port = DEFAULT_PORT;

	game.valid = 1;
	game.win = -1;
	//game.numOfPlayers = p;
	game.msg = 0;
	//game.moveCount = 0;


	// Set listner. accepting only in main loop
	sockListen = socket(AF_INET, SOCK_STREAM, 0);
	checkForNegativeValue(sockListen, "socket", sockListen);
	addrBind.sa_family = AF_INET;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(port);
	inAddr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_addr = inAddr;
	errorIndicator = myBind(sockListen, &myaddr, sizeof(addrBind));
	checkForNegativeValue(errorIndicator, "bind", sockListen);
	errorIndicator = listen(sockListen, 2);
	checkForNegativeValue(errorIndicator, "listen", sockListen);

	while (1){
		// clear set and add listner
		maxClientFd = sockListen;
		FD_ZERO(&fdSetRead);
		FD_ZERO(&fdSetWrite);
		FD_SET(sockListen, &fdSetRead);

		// add all clients to fdSetRead
		for (i = 0; i < 2; i++){
			FD_SET(ClientsQueue[i].fd, &fdSetRead);
			if (strlen(ClientsQueue[i].writeBuf) > 0){
				FD_SET(ClientsQueue[i].fd, &fdSetWrite);
			}
			if (ClientsQueue[i].fd > maxClientFd) maxClientFd = ClientsQueue[i].fd; 
		}

		// TODO: need to add timeout
		select(maxClientFd + 1, &fdSetRead, &fdSetWrite, NULL, &timeout);
		if (FD_ISSET(sockListen, &fdSetRead)){
			int fdCurr = accept(sockListen, (struct sockaddr*)NULL, NULL);
			checkForNegativeValue(fdCurr, "accept", fdCurr);
			if (fdCurr >= 0){
				if ((conPlayers) == 2) SendCantConnectToClient(fdCurr);
				else addClientToQueue(fdCurr);
			}
		}

		// Service all the sockets with input pending.
		for (i = 0; i < conPlayers; ++i){
			if (FD_ISSET(ClientsQueue[i].fd, &fdSetRead)){
				errorIndicator = receiveFromClient(i);
				if (errorIndicator < 0){
					close(ClientsQueue[i].fd);
					notifyOnWinningToPlayer(i);
				}
				else if (errorIndicator == 1){
					handleReadBuf(i);
				}

			}

			if (FD_ISSET(ClientsQueue[i].fd, &fdSetWrite)){
				errorIndicator = sendToClient(i);
				if (errorIndicator < 0){
					close(ClientsQueue[i].fd);
					notifyOnWinningToPlayer(i);
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
	n = send(ClientsQueue[index].fd, ClientsQueue[index].writeBuf, strlen(ClientsQueue[index].writeBuf), 0);
	if (n <= 0){
		//client disconected
		return -1;
	}
	strcpy(ClientsQueue[index].writeBuf, ClientsQueue[index].writeBuf + n);
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
			// it is not the client turn
			sendInvalidMoveToPlayer(index);
			return;
		}

		retVal = CheckAndMakeClientMove(data);
		clientIndexTurn = (clientIndexTurn + 1) % (conPlayers); // keep the turn moving only between connected players
		if (retVal == -1){
			sendInvalidMoveToPlayer(index);
			updateEveryoneOnMoveExceptIndex(index);
			notifyOnTurn();
		}
		else{
			updateEveryoneOnMove(index);
			if (retVal == 0) {
				notifyOnTurn();
			}
		}
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
	//newGame.numOfPlayers = game.numOfPlayers;
	//newGame.isMisere = game.isMisere;
	for (i = 0; i < HEAPS_NUM; i++) newGame.heaps[i] = game.heaps[i];
	//newGame.heapA = game.heapA;
	//newGame.heapB = game.heapB;
	//newGame.heapC = game.heapC;
	//newGame.heapD = game.heapD;
	//game.moveCount++;
	//newGame.moveCount = game.moveCount;

	createGameDataBuff(newGame, buf);
	strcat(ClientsQueue[clientIndexTurn].writeBuf, buf);
}

void notifyOnWinningToPlayer(int index){ //banko
	char buf[MSGTXT_SIZE];
	int index;

	index = conPlayers;
	//ClientsQueue[index].isPlayer = 1;

	game.isMyTurn = 0;
	game.valid = 1;
	game.win = (index == 0) ? 2 : 1;
	//game.playing = 1;

	createGameDataBuff(game, buf);
	strcat(ClientsQueue[(index + 1)%2].writeBuf, buf);
}

void updateEveryoneOnMove(int index){ //banko - whats the differnce between this and updateEveryoneOnMoveExceptIndex?  
	int i;
	char buf[MSGTXT_SIZE];

	game.myPlayerId = index; // ClientsQueue[index].clientNum;
	createGameDataBuff(game, buf);
	strcat(ClientsQueue[(index + 1) % 2].writeBuf, buf);

	//for (i = 0; i<conPlayers + conViewers; i++){
	//	game.playing = ClientsQueue[i].isPlayer;
	//	createGameDataBuff(game, buf);
	//	strcat(ClientsQueue[i].writeBuf, buf);
	//}
}

void updateEveryoneOnMoveExceptIndex(int index){ //banko - need to print: Client # made an illegal move
	//int i;
	char buf[MSGTXT_SIZE];

	game.myPlayerId = index; //ClientsQueue[index].clientNum;
	createGameDataBuff(game, buf);
	strcat(ClientsQueue[(index + 1) % 2].writeBuf, buf);

	//for (i = 0; i<conPlayers + conViewers; i++){
	//	if (i == index){
	//		continue;
	//	}
	//	game.playing = ClientsQueue[i].isPlayer;
	//	createGameDataBuff(game, buf);
	//	strcat(ClientsQueue[i].writeBuf, buf);
	//}
}

void sendInvalidMoveToPlayer(int index){
	char buf[MSGTXT_SIZE];
	game.valid = 0;
	//game.playing = ClientsQueue[index].isPlayer;

	strcpy(game.msgTxt, "");
	createGameDataBuff(game, buf);

	// restore value
	game.valid = 1;
	strcat(ClientsQueue[index].writeBuf, buf);
}

//handles msg written by client clientQueue[index]
void handleIncomingMsg(struct clientMsg data, int index){
	int i;
	char buf[MSGTXT_SIZE];
	struct gameData newGame;

	newGame.valid = 1;
	newGame.msg = index + 1; // 1/2 - message by that player . 0 - move
	//newGame.playing = ClientsQueue[index].isPlayer;

	strncpy(newGame.msgTxt, data.msgTxt, strlen(data.msgTxt));
	newGame.msgTxt[strlen(data.msgTxt)] = '\0';

	createGameDataBuff(newGame, buf);
	index == 0 ? strcat(ClientsQueue[1].writeBuf, buf) : strcat(ClientsQueue[0].writeBuf, buf);
	//if (data.recp == -1){
	//	// send to all
	//	for (i = 0; i<conViewers + conPlayers; i++) strcat(ClientsQueue[i].writeBuf, buf);
	//}
	//else{
	//	//sent to specific client. we will search for him
	//	for (i = 0; i < conViewers + conPlayers; i++){
	//		if (ClientsQueue[i].clientNum == data.recp) strcat(ClientsQueue[i].writeBuf, buf);
	//	}
	//}
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

	// -1 stands for too many clients connected
	newGame.valid = 0;
	createGameDataBuff(newGame, buf);

	errorIndicator = sendAll(fd, buf, &MSGTXT_SIZE);
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
	/*switch (clientMove.heap){
	case(0) :
		if (game.heapA < clientMove.amount) return -1;
		else{
			game.valid = 1;
			game.heapA -= clientMove.amount;
		}
		break;

	case(1) :
		if (game.heapB < clientMove.amount) return -1;
		else{
			game.valid = 1;
			game.heapB -= clientMove.amount;
		}
		break;

	case(2) :
		if (game.heapC < clientMove.amount) return -1;
		else{
			game.valid = 1;
			game.heapC -= clientMove.amount;
		}
		break;

	case(3) :
		if (game.heapD < clientMove.amount) return -1;
		else{
			game.valid = 1;
			game.heapD -= clientMove.amount;
		}
		break;

	default: return -1;
	}
*/
	if (IsBoardClear(game)){
		//if (game.isMisere){
		//	// all other clients win
		//	game.win = ClientsQueue[clientIndexTurn].clientNum;
		//	return 2;
		//}
		//else{
		
		// Client win
		game.win = clientIndexTurn; //ClientsQueue[clientIndexTurn].clientNum;
		return 1;
	}
	return 0;
}

void checkForNegativeValue(int num, char* func, int sock){
	if (num<0) close(sock);
}

int myBind(int sock, const struct sockaddr_in *myaddr, int size){
	return bind(sock, (struct sockaddr*)myaddr, sizeof(struct sockaddr_in));
}

//int MaxNum(int a, int b, int c, int d){
//	int biggest = a;
//	if (biggest < b) biggest = b;
//	if (biggest < c) biggest = c;
//	if (biggest < d) biggest = d;
//	return biggest;
//}

int IsBoardClear(struct gameData game){
	int j;
	for (j = 0; j < HEAPS_NUM; j++){
		if (game.heaps[j] != 0) return 0;
	}
	return 1;
		//game.heaps[i] == 0 &&
		//game.heapB == 0 &&
		//game.heapC == 0 &&
		//game.heapD == 0);
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
	//newClient.clientNum = minFreeClientNum;
	//newClient.isPlayer = isPlayer;
	ClientsQueue[newClientIndex] = newClient;
	conPlayers++;

	//// finding new MinFreeClientNum
	//for (minFreeClientNum = 1; minFreeClientNum<100; minFreeClientNum++){
	//	for (i = 0; i<conPlayers + conViewers; i++){
	//		if (minFreeClientNum == ClientsQueue[i].clientNum){
	//			// we found a client with the same number. need to continue to next outside iteration
	//			break;
	//		}
	//	}
	//	if (minFreeClientNum != ClientsQueue[i].clientNum){
	//		// kind of nasty code, but should work.
	//		// we are exiting main loop because we have found our number (inner loop finished)
	//		break;
	//	}
	//}


	// handling writeBuf
	newGame.valid = 1;
	newGame.msg = 0;
	newGame.isMyTurn = (conPlayers == 1) ? 1 : 0; // this is new client turn only if he is the only one here
	newGame.win = -1;
	//newGame.numOfPlayers = game.numOfPlayers;
	newGame.myPlayerId = newClientIndex;
	//newGame.playing = newClient.isPlayer;
	//newGame.isMisere = game.isMisere;
	for (i = 0; i < HEAPS_NUM; i++){
		newGame.heaps[i] = game.heaps[i];
	}
	//newGame.moveCount = game.moveCount;

	// first write to writeBuf. no worries of ruining previous data
	createGameDataBuff(newGame, ClientsQueue[newClientIndex].writeBuf);
}

/** fd - fd of client that was disconnected
return value - 1 deleted client is a player, 0 for viewer, 2 for need to notify on new turn
*/
//int delClientFromQueue(int idx){
	////////////////////*********************send win message to !idx and end the game*****************//////////////////////////

	//int i, j;
	//struct clientData delClient;

	//// find and copy deleted client
	//for (i = 0; i< conViewers + conPlayers; i++){
	//	if (ClientsQueue[i].fd == fd){
	//		delClient = ClientsQueue[i];
	//		break;
	//	}
	//}
	//j = i;
	///* move clients after deleted client to the left*/
	////for (; j< conViewers + conPlayers - 1; j++) ClientsQueue[j] = ClientsQueue[j + 1];


	///* preserve global turn*/
	//if (i < clientIndexTurn) clientIndexTurn--;
	//else if (i == clientIndexTurn && (ClientsQueue[i].isPlayer)) notifyOnTurn();

	///* update globals */
	//if (delClient.clientNum < minFreeClientNum) minFreeClientNum = delClient.clientNum;

	//if (delClient.isPlayer){
	//	conPlayers--;
	//	if (conViewers>0){
	//		notifyOnTurningToPlayer();
	//		conPlayers++;
	//		conViewers--;
	//		if (conPlayers - 1 == clientIndexTurn){
	//			notifyOnTurn();
	//		}
	//	}
	//	return 1;
	//}
	//else{
	//	conViewers--;
	//	return 0;
	//}
}



/**
clientMove - struct containing msgTxt and reciver
index - ClientsQueue index of sender
*/
void handleMsg(struct clientMsg clientMove, int index){
	struct gameData data;
	int i;
	char buf[MSGTXT_SIZE];

	data.valid = 1;
	data.msg = index;
	strcpy(data.msgTxt, clientMove.msgTxt);
	createClientMsgBuff(clientMove, buf);

	if (clientMove.recp == -1){
		// send to all except the sender
		for (i = 0; i< conPlayers + conViewers; i++){
			if (i != index){
				sendAll(ClientsQueue[i].fd, buf, &MSGTXT_SIZE);
			}
		}
	}
	else{
		// send only to a specific client number
		for (i = 0; i< conPlayers + conViewers; i++){
			if (ClientsQueue[i].clientNum == clientMove.recp){
				sendAll(ClientsQueue[i].fd, buf, &MSGTXT_SIZE);
			}
		}
	}
}


void createClientMsgBuff(struct clientMsg data, char* buf){
	if (data.msg == 0)
	{
		data.msgTxt[0] = 'a';
		data.msgTxt[1] = '\0';
	}
	sprintf(buf, "{%d$%d$%d$%d$%d$%s}",
		data.heap,
		data.amount,
		data.msg,
		data.recp,
		data.moveCount,
		data.msgTxt);
}

int parseClientMsg(char buf[MSGTXT_SIZE], struct clientMsg *data){
	return sscanf(buf, "{%d$%d$%d$%d$%d$%[^}]",
		&data->heap,
		&data->removes,
		&data->msg,
		&data->msgTxt[0]);
		//&data->recp,
		//&data->moveCount,
		
}

int parseGameData(char buf[MSGTXT_SIZE], struct gameData* data){
	return sscanf(buf, "{%d$%d$%d$%d$%d$%d$%d$%d$%d$%d$%[^}]",
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
		//data.playing,
		//data.isMisere,
		data.LastTurnHeap, //banko
		data.LastTurnRemoves, //banko
		data.heaps[0],
		data.heaps[1],
		data.heaps[2],
		//data.heapD,
		//data.moveCount,
		data.msgTxt);
}
