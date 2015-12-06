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
#include <sys/time.h>

// Messages
#define ARGS_ERROR "The program accepts just three or four command-line arguments\n"
// Constants
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 6444
#define HEAPS_NUM 3
#define BUF_SIZE 300
#define MSGTXT_SIZE 200
#define WAITING_TIME 60

// Structs
struct gameData{
	int valid;
	int msg;			// 1/2 - message by that player . 0 - move
	int isMyTurn;		// 0 - no, 1 - yes
	int win;			//-1 game on, <player id> - the player id who won, 2 - other player disconnected
	int myPlayerId;		// player id (0/1)
	int LastTurnHeap;	//which heap was removed from.
	int LastTurnRemoves;//how many cubes removed from that heap
	int heaps[3];		//the status of each heap
	char msgTxt[200];	//free text space
};

struct clientMsg{
	int heap;			//which heap to remove from
	int removes;		//how many cubes to remove
	int msg;			// 1 - this is a message, 0 - this is a move
	char msgTxt[200];	//free text space
};

struct clientData{
	int fd;					// fd that was returned from select
	char readBuf[BUF_SIZE];	// contains data read from client
	char writeBuf[BUF_SIZE];// contains data to write to client
};

struct clientData clients[2];	// the data of the two clients

// Globals
int clientIndexTurn = 0;			// current turn of client
int conPlayers = 0;					// amount of connected players
struct gameData game;				// global game struct
int LastTurnHeap = 0;				//the last turn's heap.
int LastTurnRemoves = 0;			//the last turn number of removes

// Declarations
int myBind(int sock, const struct sockaddr_in *myaddr, int size);
int IsBoardClear(struct gameData game);
int CheckAndMakeClientMove(struct clientMsg clientMove);
int send_all(int s, char *buf, int *len);
void opCheck(int num, char* func, int sock);
int parseClientMsg(char buf[MSGTXT_SIZE], struct clientMsg *data);
void createClientMsgBuff(struct clientMsg data, char* buf);
void createGameDataBuff(struct gameData data, char* buf);
int parseGameData(char buf[MSGTXT_SIZE], struct gameData* data);
void SendCantConnectToClient(int fd);
void sendInvalidMoveToPlayer(int index);
void notifyOnTurn();
void notifyOnDisconnectionToPlayer(int index);
void notifyOnWinningToBoth(int index);
void addClient(int newFd);
void handleMsg(struct clientMsg, int index);
void handleIncomingMsg(struct clientMsg data, int index);
void handleReadBuf(int index);
int receiveFromClient(int index);
int sendToClient(int index);


int main(int argc, char** argv){
	int sockListen, errorIndicator, maxClientFd;
	int pieces, port, i, j;
	fd_set fdSetRead, fdSetWrite;
	struct sockaddr_in myaddr;
	struct sockaddr addrBind;
	struct in_addr inAddr;
	
	clock_t start, end; 
	struct timeval timeout;
	timeout.tv_sec = WAITING_TIME;
	timeout.tv_usec = 0;

	// Arguments validty
	if (argc < 4 || argc > 5){
		printf(ARGS_ERROR);
		exit(1);
	}
	
	// Gets the number of pieces per heap and the current port
	for (j = 0; j < 3; j++){
		sscanf(argv[j + 1], "%d", &pieces);
		game.heaps[j] = pieces;
	}
	if (argc == 5) sscanf(argv[4], "%d", &port);
	else port = DEFAULT_PORT;

	// Initializes the game data
	game.valid = 1;
	game.win = -1;
	game.msg = 0;
	game.LastTurnHeap = 0;
	game.LastTurnRemoves = 0;

	// Set listner, accepting only in main loop
	sockListen = socket(AF_INET, SOCK_STREAM, 0);
	opCheck(sockListen, "socket", sockListen);
	addrBind.sa_family = AF_INET;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(port);
	inAddr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_addr = inAddr;
	errorIndicator = myBind(sockListen, &myaddr, sizeof(addrBind));
	opCheck(errorIndicator, "bind", sockListen);
	errorIndicator = listen(sockListen, 2);
	opCheck(errorIndicator, "listen", sockListen);

	while (1){
		// Clear set and add listner
		maxClientFd = sockListen;
		FD_ZERO(&fdSetRead);
		FD_ZERO(&fdSetWrite);
		FD_SET(sockListen, &fdSetRead);

		// Add the clients to fdSetRead
		for (i = 0; i < conPlayers; i++){
			FD_SET(clients[i].fd, &fdSetRead);
			if (strlen(clients[i].writeBuf) > 0) FD_SET(clients[i].fd, &fdSetWrite);
			if (clients[i].fd > maxClientFd) maxClientFd = clients[i].fd; // Updates the max client fd
		}

		start = clock(); // Set the clock for the current time, for timeout uses
		if (select(maxClientFd + 1, &fdSetRead, &fdSetWrite, NULL, &timeout) == 0){
			close(clients[clientIndexTurn].fd);
			notifyOnDisconnectionToPlayer(clientIndexTurn);
		}
		if (FD_ISSET(sockListen, &fdSetRead)){
			int fdCurr = accept(sockListen, (struct sockaddr*)NULL, NULL);
			opCheck(fdCurr, "accept", fdCurr);
			if (fdCurr >= 0){
				if (conPlayers == 2) SendCantConnectToClient(fdCurr);
				else{
					addClient(fdCurr);
					if (conPlayers == 2) notifyOnTurn();
					timeout.tv_sec = WAITING_TIME; // Restart the waiting time to one min
				}
			}
		}

		// Service sockets with input
		for (i = 0; i < conPlayers; i++){
			if (FD_ISSET(clients[i].fd, &fdSetRead)){
				errorIndicator = receiveFromClient(i);
				if (errorIndicator <= 0){
					close(clients[i].fd);
					notifyOnDisconnectionToPlayer(i);
				}
				else if (errorIndicator == 1){
					if (i != clientIndexTurn){
						end = clock();
						int clockCnt = (int)(end - start) / CLOCKS_PER_SEC;
						timeout.tv_sec = WAITING_TIME - clockCnt;
					}
					else timeout.tv_sec = WAITING_TIME;
					handleReadBuf(i);
				}
			}

			if (FD_ISSET(clients[i].fd, &fdSetWrite)){
				errorIndicator = sendToClient(i);
				if (errorIndicator < 0){
					close(clients[i].fd);
					notifyOnDisconnectionToPlayer(i);
				}
			}
		}
	}
}

int sendToClient(int index){
	int n;
	if (strlen(clients[index].writeBuf) == 0) return 0; //nothing to send
	n = send(clients[index].fd, clients[index].writeBuf, strlen(clients[index].writeBuf), 0);
	if (n <= 0) return -1; //client disconnected
	strcpy(clients[index].writeBuf, clients[index].writeBuf + n);
	return 1;
}

//move or msg from clients[index]
void handleReadBuf(int index){
	struct clientMsg data;
	int retVal, i;
	parseClientMsg(clients[index].readBuf, &data);
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
		else if (retVal == 1) notifyOnWinningToBoth(index);
		else notifyOnTurn();
	}

	// deleting read data from readBuf.shouldn't happen. just in case
	for (i = 0; i < MSGTXT_SIZE; ++i){
		if (clients[index].readBuf[i] == '}'){
			clients[index].readBuf[i] = '\0';
			if (clients[index].readBuf[i + 1] == '{'){
				strcpy(clients[index].readBuf, clients[index].readBuf + i + 1);
				break;
			}
		}
		clients[index].readBuf[i] = '\0';
	}
}

void notifyOnTurn(){
	char buf[MSGTXT_SIZE];
	struct gameData newGame;
	int i;

	newGame.isMyTurn = 1;
	newGame.valid = 1;
	newGame.msg = 0;
	newGame.win = game.win;
	newGame.myPlayerId = clientIndexTurn;
	for (i = 0; i < HEAPS_NUM; i++) newGame.heaps[i] = game.heaps[i];
	newGame.isMyTurn = 1;
	createGameDataBuff(newGame, buf);
	strcpy(clients[clientIndexTurn].writeBuf, buf);
	newGame.isMyTurn = 0;
	newGame.myPlayerId = 1 - clientIndexTurn;
	createGameDataBuff(newGame, buf);
	if (strlen(clients[1 - clientIndexTurn].writeBuf) == 0){//no illegal move done
		strcpy(clients[1 - clientIndexTurn].writeBuf, buf);
	}
}

void notifyOnDisconnectionToPlayer(int index){
	char buf[MSGTXT_SIZE];
	strcpy(game.msgTxt, "\0");
	game.isMyTurn = 0;
	game.win = 2;
	game.myPlayerId = 1 - index;
	createGameDataBuff(game, buf);
	strcpy(clients[1 - index].writeBuf, buf);
	sendToClient(1 - index);
	close(clients[1 - index].fd);
	exit(0);
}

void notifyOnWinningToBoth(int index){
	int i;
	char buf[MSGTXT_SIZE];
	game.myPlayerId = index; 
	game.win = index;
	for (i = 0; i < conPlayers; i++){
		createGameDataBuff(game, buf);
		strcat(clients[i].writeBuf, buf);
	}
}

void sendInvalidMoveToPlayer(int index){
	char buf[MSGTXT_SIZE];
	game.valid = 0;
	strcpy(game.msgTxt, "\0");
	createGameDataBuff(game, buf);
	game.valid = 1; // restore value
	strcpy(clients[index].writeBuf, buf);
}

//handles msg written by client
void handleIncomingMsg(struct clientMsg data, int index){
	char buf[MSGTXT_SIZE];
	struct gameData newGame;

	newGame.valid = 1;
	newGame.msg = 1; // 1/2 (index + 1) - message by that player . 0 - move
	newGame.LastTurnHeap = 0;
	newGame.LastTurnRemoves = 0;
	newGame.isMyTurn = 0;
	newGame.myPlayerId = 0;
	newGame.win = -1;
	newGame.heaps[0] = 0;
	newGame.heaps[1] = 0;
	newGame.heaps[2] = 0;
	strncpy(newGame.msgTxt, data.msgTxt, strlen(data.msgTxt));
	newGame.msgTxt[strlen(data.msgTxt)] = '\0';
	createGameDataBuff(newGame, buf);
	strcpy(clients[1 - index].writeBuf, buf);
}


// receive data from client
int receiveFromClient(int index){
	int len = strlen(clients[index].readBuf), n;
	if (strlen(clients[index].readBuf) < 10) len = 0;
	n = recv(clients[index].fd, clients[index].readBuf + len, MSGTXT_SIZE * 5, 0);
	if (n <= 0) return -1; //client disconected
	const char* startPtr = strchr(clients[index].readBuf, '{');
	const char* endPtr = strchr(clients[index].readBuf, '}');
	if (startPtr && endPtr) return 1;
	return 0;
} //returns: 1 for read all, 0 for read partial data, -1 for disconnected client

void SendCantConnectToClient(int fd){
	int errorIndicator;
	char buf[MSGTXT_SIZE];
	struct gameData newGame;
	int msgtxt_size = MSGTXT_SIZE;
	newGame.valid = 0;
	createGameDataBuff(newGame, buf);
	errorIndicator = send_all(fd, buf, &msgtxt_size);
	opCheck(errorIndicator, "send", fd);
	close(fd);
}

int CheckAndMakeClientMove(struct clientMsg clientMove){ 
	if (clientMove.heap<0 || clientMove.heap>(HEAPS_NUM - 1)) return -1;
	if (game.heaps[clientMove.heap] < clientMove.removes) return -1;
	else{
		game.valid = 1;
		game.heaps[clientMove.heap] -= clientMove.removes;
	}
	if (IsBoardClear(game)){
		game.win = clientIndexTurn; 
		return 1;
	}
	return 0;
} //returns: -1 invalid move, 1 valid move - client won, 0 valid move - nobody won

void opCheck(int num, char* func, int sock){
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
	for (j = 0; j < HEAPS_NUM; j++) if (game.heaps[j] != 0) return 0;
	return 1;
}

int send_all(int s, char *buf, int *len) {
	int total = 0, bytesleft = *len, n;
	while (total < *len) {
		n = send(s, buf + total, bytesleft, 0);
		if (n == 0){
			close(s);
			exit(1);
		}
		if (n == -1) { break; }
		total += n;
		bytesleft -= n;
	}
	*len = total;
	return n == -1 ? -1 : 0; // -1 on failure, 0 on success
}

void addClient(int newFd){
	struct clientData newClient;
	int newClientIndex, i;
	struct gameData newGame;
	// add new cliant to clients
	newClientIndex = conPlayers;
	newClient.fd = newFd;
	clients[newClientIndex] = newClient;
	conPlayers++;
	newGame.valid = 1;
	newGame.msg = 0;
	newGame.isMyTurn = (conPlayers == 1) ? 1 : 0; // this is new client turn only if he is the only one here
	newGame.win = -1;
	newGame.myPlayerId = newClientIndex;
	for (i = 0; i < HEAPS_NUM; i++) newGame.heaps[i] = game.heaps[i];
	createGameDataBuff(newGame, clients[newClientIndex].writeBuf);
}

void handleMsg(struct clientMsg clientMove, int index){ //index - clients index of sender
	struct gameData data;
	char buf[MSGTXT_SIZE];
	int msg_size = MSGTXT_SIZE;
	data.valid = 1;
	data.msg = 1;
	strcpy(data.msgTxt, clientMove.msgTxt);
	createClientMsgBuff(clientMove, buf);
	send_all(clients[1 - index].fd, buf, &msg_size);
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
}

int parseClientMsg(char buf[MSGTXT_SIZE], struct clientMsg *data){
	int x = sscanf(buf, "{%d$%d$%d$%[^}]",
		&data->heap,
		&data->removes,
		&data->msg,
		&data->msgTxt[0]);
	LastTurnHeap = data->heap;
	LastTurnRemoves = data->removes;
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
		LastTurnHeap,
		LastTurnRemoves,
		data.heaps[0],
		data.heaps[1],
		data.heaps[2],
		data.msgTxt);
}