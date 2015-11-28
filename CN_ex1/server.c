//commet
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h> //Sockets
#include <netinet/in.h> //Internet addresses
#include <arpa/inet.h> //Working with Internet addresses
#include <netdb.h> //Domain Name Service (DNS)
#include <sys/select.h> //Select
#include <errno.h> //Working with errno to report errors
#include <sys/time.h> 

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
#define OP_ERROR "Error: %s\n"


// Constants
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT "6444"
#define HEAPS_NUM 3
#define BUF_SIZE 300
#define MSGTXT_SIZE 255

//Structures 
typedef struct state{
	int myTurn;
	int valid;
	int win;
	int heaps[3];
} Game_state;

typedef struct move{
	int heap;
	int removes;
	char msg[MSG_SIZE];
} Move;

typedef struct client{
	int fd;
	char readBuf[MSG_SIZE];		// contains data read from client
	char writeBuf[MSG_SIZE]; 	// contains data to write to client
} Client;

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

struct clientMsg{
	int heap;
	int removes;
	int msg; // 1 - this is a message, 0 - this is a move
	char msgTxt[255];
};

//Globals
int msg_len = MSG_SIZE;
Client clients[CLIENTS_NUM];
int players_cnt = 0;

//Declarations
void op_check(int num, char *func, int sock);
int my_bind(int sock, const struct sockaddr_in *myaddr, int size);
void is_win_pose(Game_state *game, int winner);
void exc_server_move(Game_state *game);
void exc_client_move(Game_state *game, Move *client_move);
int send_all(int s, char *buf, int *len);
int receive_all(int s, char *buf, int *len);

int main(int argc, char **argv){
	int sock;
	struct sockaddr_in myaddr;
	struct sockaddr addrBind;
	struct in_addr inAddr;
	char buf[MSG_SIZE];
	fd_set read_fds, write_fds;

	int i, port;
	int fdmax;

	Move *client_move = malloc(sizeof(Move));
	Game_state *game = malloc(sizeof(Game_state));
	// Initializes the game state and validates the input
	if (argc < 4 || argc > 5){
		printf(ILLEGAL_ARGS);
		free(client_move);
		free(game);
		exit(1);
	}
	for (i = 0; i < HEAPS_NUM; i++){
		sscanf(argv[i + 1], "%d", &game->heaps[i]);
		if (game->heaps[i]<1 || game->heaps[i]>1000){
			printf(ILLEGAL_ARGS);
			free(client_move);
			free(game);
			exit(1);
		}
	}
	if (argc == 5) sscanf(argv[4], "%d", &port);
	else port = DEFAULT_PORT;
	game->valid = 1;
	game->win = 0;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	op_check(sock, "socket", sock);
	addrBind.sa_family = AF_INET;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(port);
	inAddr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_addr = inAddr;
	op_check(my_bind(sock, &myaddr, sizeof(addrBind)), "bind", sock);
	op_check(listen(sock, CLIENTS_NUM), "listen", sock);
	/*sock = accept(sock, (struct sockaddr*)NULL, NULL);
	op_check(sock, "accept", sock);

	sprintf(buf, "%d$%d$%d$%d$%d", game->valid, game->win, game->heaps[0], game->heaps[1], game->heaps[2]);
	*/
	//Server game loop
	while (1){
		FD_ZERO(&read_fds); //Reset fd set
		FD_SET(sock, &read_fds);
		fdmax = 0;
		struct timeval timeout;
		timeout.tv_sec = 60; // 60 sec == one min
		timeout.tv_sec = 0;

		for (i = 0; i < players_cnt; i++){
			FD_SET(clients[i].fd, &read_fds); //Add listening socket
			if (strlen(clients[i].writeBuf) > 0) FD_SET(clients[i].fd, &write_fsd);
			if (clients[i].fd > fdmax) fdmax = clients[i].fd; 
		}

		select(fdmax + 1, &read_fds, &write_fsd, NULL, timeout.tv_sec);

		if (FD_ISSET(sock, &read_fds)){
			int new_sock = accept(sock, (struct sockaddr*)NULL, NULL);
			op_check(sock, "accept", new_sock);
			if (new_sock >= 0){
				if (players_cnt > CLIENTS_NUM) SendCantConnectToClient(new_sock);
				else addClient(new_sock, game);
			} 
		}
		/****************************************************************************************/
		for (i = 0; i < players_cnt; i++){
			if (FD_ISSET(clients[i].fd, &read_fds)){
				int received = receive_all(clients[i].fd, clients[i].readBuf, &msg_len);
				op_check(received, "recv", clients[i].fd);
			
			if (FD_ISSET(clients[i].fd, &write_fds)){
				errorIndicator = sendToClient(i);
				if (errorIndicator < 0){
					close(ClientsQueue[i].fd);
					delClientFromQueue(ClientsQueue[i].fd);
				}
			}
		}



		op_check(send_all(sock, buf, &msg_len), "send", sock);
		if (game->win != 0){ 
			// shutdown(sock, SHUT_RD); //bonus
			close(sock);
			free(client_move);
			free(game);
			exit(0); // If the game is over the server disconnect
		}
		op_check(receive_all(sock, buf, &msg_len), "recv", sock);
		sscanf(buf, "%d$%d", &client_move->heap, &client_move->removes);
		exc_client_move(game, client_move);
		is_win_pose(game, 1); // Check for winning pose. if it is a winnig pose, the client (1) wins 
		if (game->win == 0){
			exc_server_move(game);
			is_win_pose(game, 2); // Check for a winning pose. if it is a winnig pose, the server (2) wins
		}
		sprintf(buf, "%d$%d$%d$%d$%d", game->valid, game->win, game->heaps[0], game->heaps[1], game->heaps[2]);
	}
	free(client_move);
	free(game);
}

// Executes the client's move, if it's valid
void exc_client_move(Game_state *game, Move *client_move){ 
	if (client_move->removes<1) game->valid = 0;  // If the removes field equals zero, the move is invalid
	else if (game->heaps[client_move->heap]<client_move->removes) game->valid = 0;
	else {
		game->valid = 1;
		game->heaps[client_move->heap] -= client_move->removes;
	}
}

//Checks if the operation returned a negative number and prints an error if needed
void op_check(int num, char *func, int sock){
	if (num < 0){
		printf(OP_ERROR, strerror(errno));
		// shutdown(sock, SHUT_RD); //bonus
		close(sock);
		exit(1);
	}
}

//Changes the bing args
int my_bind(int sock, const struct sockaddr_in *myaddr, int size){
	return bind(sock, (struct sockaddr*)myaddr, sizeof(struct sockaddr_in));
}

//Executes the server move, removes one piece from the biggest heap
void exc_server_move(Game_state *game){
	int heap = 0;
	int i;
	for (i = 1; i < HEAPS_NUM; i++){ //finds the first heap with the highest number of pieces
		if (game->heaps[heap]<game->heaps[i]){
			heap = i;
		}
	}
	game->heaps[heap] -= 1;
	return;
}

// Helper func - checks if it is a winnig pose
void is_win_pose(Game_state *game, int winner){
	int i;
	for (i = 0; i < HEAPS_NUM; i++){
		if (game->heaps[i]>0) return;
	}
	game->win = winner;
}

// Sends all the data to the client
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

// Receives all the data from the client
int receive_all(int s, char *buf, int *len) {
	int total = 0; // how many bytes we've received
	size_t bytesleft = *len; // how many we have left to receive
	int n;

	while (total < *len) {
		n = recv(s, buf + total, bytesleft, 0);
		if (n == -1) break;
		total += n;
		bytesleft -= n;
	}
	*len = total; // return number actually received here
	return n == -1 ? -1 : 0; // -1 on failure, 0 on success
}
int receiveFromClient(int index){
	// int total = 0;  how many bytes we've received 
	// size_t bytesleft = *len;  how many we have left to receive 
	int n;
	//char temp[MSG_SIZE];

	// buf + strlen(buf) guaranties no override
	////printf("on receive len:%lu\n", strlen(ClientsQueue[index].readBuf));
	int len = strlen(ClientsQueue[index].readBuf);
	if (strlen(ClientsQueue[index].readBuf) < 10)
	{
		len = 0;
	}
	n = recv(ClientsQueue[index].fd, ClientsQueue[index].readBuf + len, MSG_SIZE * 5, 0);

	if (n <= 0){
		//client disconected
		return -1;
	}

	////printf("bufer received: %s\n",ClientsQueue[index].readBuf);
	const char* startPtr = strchr(ClientsQueue[index].readBuf, '{');
	const char* endPtr = strchr(ClientsQueue[index].readBuf, '}');
	//if(sscanf(ClientsQueue[index].readBuf,"{%s}",temp) ==1){
	if (startPtr && endPtr)
	{
		//printf("index:%d, num:%d, socket:%d, has full msg in his buffer:%s\n",index,ClientsQueue[index].clientNum,ClientsQueue[index].fd,ClientsQueue[index].readBuf);
		return 1;
	}
	return 0;
}

void addClient(int newFd, Game_state *game){
	Client new_client;
	Game_state new_game;
	int i;

	new_client.fd = newFd;
	clients[players_cnt] = new_client;
	players_cnt++;

	new_game.valid = 1;
	//new_game.msg = 0;
	new_game.myTurn = (players_cnt == 1) ? 1 : 0; // this is new client turn only if he is the only one here
	new_game.win = 0;
	for (i = 0; i < HEAPS_NUM; i++) new_game.heaps[i] = game.heaps[i];
	// first write to writeBuf. no worries of ruining previous data
	createGameDataBuff(new_game, clients[players_cnt -1].writeBuf);
}

// CHANGE THIS SHIT!!!! ***************************************
void SendCantConnectToClient(int fd){
	int errorIndicator;
	char buf[MSG_SIZE];
	struct gameData newGame;

	// -1 stands for too many clients connected
	newGame.valid = 0;
	createGameDataBuff(newGame, buf);

	errorIndicator = sendAll(fd, buf, &msg_SIZE);
	checkForNegativeValue(errorIndicator, "send", fd);

	close(fd);
}

