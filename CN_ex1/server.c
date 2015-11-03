#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h> 
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h> //Sockets
#include <netinet/in.h> //Internet addresses
#include <arpa/inet.h> //Working with Internet addresses
#include <netdb.h> //Domain Name Service (DNS)
#include <errno.h> //Working with errno to report errors

// constants and messages
#define ILLEGAL_ARGS "Illegal arguments\n"
#define OP_ERROR "Error: %s\n"
#define DEFAULT_PORT 6444
#define HEAPS_NUM 3
#define BUF_SIZE 50

//Globals
int msg_len = BUF_SIZE;

//Structures 
typedef struct state{
	int valid;
	int win;
	int heaps[3];
} Game_state;

typedef struct move{
	int heap;
	int removes;
} Move;

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
	char buf[BUF_SIZE];

	int i, port;
	Move *client_move = malloc(sizeof(Move));
	Game_state *game = malloc(sizeof(Game_state));
	// Initializes the game state and validates the input
	if (argc < 4 || argc > 5){
		printf(ILLEGAL_ARGS);
		exit(1);
	}
	for (i = 0; i < HEAPS_NUM; i++){
		sscanf(argv[i + 1], "%d", &game->heaps[i]);
		if (game->heaps[i]<1 || game->heaps[i]>1000){
			printf(ILLEGAL_ARGS);
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
	op_check(listen(sock, 5), "listen", sock);
	sock = accept(sock, (struct sockaddr*)NULL, NULL);
	op_check(sock, "accept", sock);

	sprintf(buf, "%d$%d$%d$%d$%d", game->valid, game->win, game->heaps[0], game->heaps[1], game->heaps[2]);
	
	//Server game loop
	while (1){
		op_check(send_all(sock, buf, &msg_len), "send", sock);
		if (game->win != 0){ 
			// bonus? shutdown(int sock, 0);
			close(sock);
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
		// bonus? shutdown(int sock, 0);
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