#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h> 
#include <time.h> 
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h> //Sockets
#include <netinet/in.h> //Internet addresses
#include <arpa/inet.h> //Working with Internet addresses
#include <netdb.h> //Domain Name Service (DNS)
#include <errno.h> //Working with errno to report errors

// Messages
#define ILLEGAL_ARGS "Illegal arguments\n"
#define SOCKET_ERROR "Error opening the socket: %s\n"
#define YOUR_TURN "your turn:\n"
#define ILLEGAL_INPUT "Illegal input! Game over"
#define CONNECTION_ERROR "failed to connect\n"
#define RECEIVE_ERROR "failed to receive data from server\n"
#define LEGAL_MOVE "Move accepted\n"
#define ILLEAGAL_MOVE "Illegal move\n"
#define CLIENT_WIN "You win!\n"
#define SERVER_WIN "Server win!\n"
// Constants
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT "6444"
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
int server_connect(int sock, const char *address, char *port);
void print_heaps(Game_state *game);
void print_winner(Game_state *game);
void receive_data(int sock, Game_state *game);
void print_is_valid_move(Game_state *game);
char *input2str(FILE *pFile);
void get_client_move(int sock, Move *curr_move);
int send_all(int s, char *buf, int *len);
int receive_all(int s, char *buf, int *len);
void free_all(char* address, char* port, Move *move, Game_state *game);


int main(int argc, char **argv){
	char *port;
	char *address;

	// Initializes the game state and validates the input
	if (argc<1 || argc>3){ 
		printf(ILLEGAL_ARGS);
		exit(1);
	}
	if (argc == 1 || argc == 2){
		port = DEFAULT_PORT;
		if (argc == 1) address = DEFAULT_HOST;
	}
	if (argc == 2 || argc == 3){
		address = malloc(sizeof(char)*strlen(argv[1]) + 1);
		strcpy(address, argv[1]);
		if (argc == 3){
			port = malloc(sizeof(char)*strlen(argv[2]) + 1);
			strcpy(port, argv[2]);
		}
	}

	int sock = socket(AF_INET, SOCK_STREAM, 0); // Get socket
	if (sock == -1){
		printf(SOCKET_ERROR, strerror(errno));
		return errno;
	}
	sock = server_connect(sock, address, port); // Connect to server
	char buf[BUF_SIZE];
	Move *curr_move = malloc(sizeof(Move));
	Game_state *game = malloc(sizeof(Game_state));
	receive_data(sock, game); // Get initial data
	print_heaps(game);

	//Client game loop
	while (game->win == 0){
		printf(YOUR_TURN);

		get_client_move(sock, curr_move);
		sprintf(buf, "%d$%d", curr_move->heap, curr_move->removes);
		if (send_all(sock, buf, &msg_len) == -1){
			close(sock);
			free_all(address, port, curr_move, game);
			exit(0);
		}
		receive_data(sock, game); // Refresh the data
		print_is_valid_move(game); // Check if move was valid
		if (game->win == 0) print_heaps(game); // keep on playing
	}
	print_winner(game);
	free_all(address, port, curr_move, game);
	return 0;
}

// Connects to the the server
int server_connect(int sock, const char* address, char* port){
	struct addrinfo hints, *servinfo, *p;
	int rv;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
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
		fprintf(stderr, CONNECTION_ERROR);
		close(sock);
		exit(2);
	}

	freeaddrinfo(servinfo); // all done with this structure
	return sock;
}

 //Handles client input (unknown length), returns a string without redundant white spaces after each new line
char* input2str(FILE* pFile){ 
	char *str;
	char ch;
	char p_ch = '~';
	size_t size = 10;
	size_t len = 0;
	str = malloc(sizeof(char)*size);
	ch = fgetc(pFile);
	while (ch != EOF && ch != '\n')
	{
		if ((p_ch != '~' && p_ch != ' ') || (ch != ' ')){
			str[len++] = ch;
			if (len == size)
			{
				size = 2 * size;
				str = realloc(str, sizeof(char)*size);
			}
			p_ch = ch;
			ch = fgetc(pFile);
		}
		else{
			p_ch = ch;
			ch = fgetc(pFile);
		}
	}
	str[len++] = '\0';
	str = realloc(str, sizeof(char)*len);
	return str;
}


// Gets the client's cmd and handles it, return a new move to be execute by the server
void get_client_move(int sock, Move *curr_move){
	char * cmd;
	char * word1;
	char * word2;

	cmd = input2str(stdin);
	if (strcmp(cmd, "Q") == 0){
		// bonus? shutdown(sock, 0); 
		close(sock);
		free(cmd);
		exit(0);
	}
	word1 = strtok(cmd, " ");
	word2 = strtok(NULL, " ");
	if (strlen(word1) != 1 || (word1[0] - 'A') < 0 || (word1[0] - 'A' - 1) > HEAPS_NUM || atoi(word2) == 0){ // Verifies the move
		curr_move->heap = 0;
		curr_move->removes = 0; // The removes of an invalid move will be marked with zero  
	}
	else {
		curr_move->heap = (int)(word1[0] - 'A');
		curr_move->removes = atoi(word2);
	}
	free(cmd);
	return;
}

// Handles the data that received from the server
void receive_data(int sock, Game_state *game){
	char buf[msg_len];
	int rec = receive_all(sock, buf, &msg_len);
	if (rec == -1)
	{
		fprintf(stderr, RECEIVE_ERROR);
		close(sock);
		exit(2);
	}
	sscanf(buf, "%d$%d$%d$%d$%d", &game->valid, &game->win, &game->heaps[0], &game->heaps[1], &game->heaps[2]);
	return;
}

// Prints an error if the move is illeagal or announce that the move accepted
void print_is_valid_move(Game_state * game){
	if (game->valid == 1) printf(LEGAL_MOVE);
	else printf(ILLEAGAL_MOVE);
}

// Prints the game winner
void print_winner(Game_state * game){
	if (game->win == 1) printf(CLIENT_WIN);
	else if (game->win == 2) printf(SERVER_WIN);
}

// Prints the number of pieces in every heap
void print_heaps(Game_state * game){
	printf("Heap A: %d\n", game->heaps[0]);
	printf("Heap B: %d\n", game->heaps[1]);
	printf("Heap C: %d\n", game->heaps[2]);
}

// Sends all the data to the server
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

// Receives all the data from the server
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

void free_all(char* address, char* port, Move *move, Game_state *game){
	if (strcmp(address, DEFAULT_HOST) != 0) free(address);
	if (strcmp(port, DEFAULT_PORT) != 0) free(port);
	free(move);
	free(game);
}
