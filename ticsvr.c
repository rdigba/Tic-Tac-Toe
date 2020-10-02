#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h> 
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <ctype.h>

char board[9]; // Global board variable
int port = 3000; // Default to port 3000 if -p is not present
static int listenfd; // For the listen socket

//----------------------------------------------------
// Client struct to create a linked list structure
struct client {
    int fd; // File descriptor we will be writing to
    int bytes_in_buf; // Used for reading from them
    char role; // x or o if they are playing
    char *buf; // Used for reading from them
    struct in_addr ipaddr; // Ip adderss
    struct client *next; // Linked list feature
} *top = NULL;
// ---------------------------------------------------

int howmany = 0; // Represents how many clients are connected
int player_index = 0; // This will represent the index in array "xo"[player_index] to select who gets assigned what role
int turn = 0; // 0 for player x turn, and 1 for player o turn
int o_present = 0; // Use this to show a player is currently o

// Helper functions from muffinman.c
static void addclient(int fd, struct in_addr addr);
static void removeclient(int fd);
static void broadcast(char *s, int size);

// My own helper functions
static void makemove(char player, int move);

int main(int argc, char **argv) {

	// Setting up helper functions (from usefule code and muffinman.c)
	extern void showboard(int fd);
	extern int game_is_over();
	extern int allthree(int start, int offset);
	extern int isfull();
	extern void bindandlisten();
	extern void newconnection();
	extern void restart();
	// Pointer to linked list of clients
	struct client *p;


	// Prepare to parse command line arguments
	int c, status = 0;
	int option = 0;

	// Name of the prorgam
	char *progname = argv[0];

	// Parse Command Line Arguments
	while ((c = getopt(argc, argv, "p:")) != EOF) {
		switch(c) {
			case 'p':
				port = atoi(optarg);
				option = 1;
				break;
			case '?':
			default:
				fprintf(stderr, "usage: %s [-p port]\n", progname);
				return 1;
		}
	}

	if (argc != 1 && !option) {
		fprintf(stderr, "usage: %s [-p port]\n", progname);
		return 1;
	}
	
	// Initializing board
	memcpy(board, "123456789", 9);

	bindandlisten();

	// the only way the server exits is by being killed
	while (1) {
		fd_set fds;
		int maxfd = listenfd;
		FD_ZERO(&fds);
		FD_SET(listenfd, &fds);
		// Loop through clients and add them to fds set every loop
		for (p = top; p; p = p->next) {
		    FD_SET(p->fd, &fds);
		    if (p->fd > maxfd)
			maxfd = p->fd;
		}
		// Set up select call to communicate with multiple clients without blocking
		if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
	    	perror("select");
		}
		else {
			// Use this for loop to find the client from the linked list that is ready to be read from
			for (p = top; p; p = p->next) {
				if (FD_ISSET(p->fd, &fds)) {
				    break;
				}
			}

			// If client is ready to talk, let em
			if(p) {
				int len;
				len = read(p->fd, p->buf + p->bytes_in_buf, sizeof p->buf - p->bytes_in_buf);
				if (len == 0) {
					// Disconnected 
					printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
					removeclient(p->fd);
				}
				// Client writes a message
				else if (len > 0) {
					if (!strchr(p->buf, '\n')) {
						char *msg = malloc(100);
						strcat(msg, p->buf);
						strcat(msg, "\r\n");
						printf("chat message: %s\n", msg);
						struct client *sender = p;
						for (p = top; p; p = p->next) {
							if (p != sender) {
								write(p->fd, msg, sizeof msg - 1);
							}
						}
						continue;
					}
					// If the length of message is 1 and a digit, it is a move
					if (strlen(p->buf) == 2 && isdigit(p->buf[0]) && p->buf[0] != '0') {
						if (turn == 0 && p->role == 'x') {
							// Player tried to repeat move
							if (board[(p->buf[0] - '0') - 1] == 'x' || board[(p->buf[0] - '0') - 1] == 'o') {
								static char msg[] = "Move was already made here\r\n";
								write(p->fd, msg, sizeof msg - 1);
							}
							else {
								makemove(p->role, p->buf[0] - '0');
							}
						}
						else if (turn == 1 && p->role == 'o') {
							if (board[(p->buf[0] - '0') - 1] == 'x' || board[(p->buf[0] - '0') - 1] == 'o') {
								static char msg[] = "Move was already made here\r\n";
								write(p->fd, msg, sizeof msg - 1);
							}
							else {
								makemove(p->role, p->buf[0] - '0');
							}
						}	
						else {
							static char msg[] = "It is not your turn\r\n";
							write(p->fd, msg, sizeof msg - 1);
						}
					}
					// Otherwise it is a message
					else {
						// Set up to send msg to every1 but sender
						char *pos = strchr(p->buf, '\n'); // Find newline character then make character on the right the null terminator
						*(pos + 1) = '\0';
						char *msg = malloc(100);
						strcat (msg, p->buf);
						strcat(msg, "\r\n");
						printf("chat message: %s", msg);
						p->bytes_in_buf = 0;
						p->buf = malloc(100);
						struct client *sender = p;
						for (p = top; p; p = p->next) {
							if (p != sender) {
								write(p->fd, msg, sizeof msg - 1);
							}
						}
					}
				}
				// Shouldn't reach here
				else {
					perror("read");
					exit(1);
				}
			}
			// Enable new conncetions if they are made
			if (FD_ISSET(listenfd, &fds)) {
				newconnection();
			}
		}
		if (game_is_over()) {
			int c = game_is_over();
			// If somebody won
			if (c == 'x' || c == 'o') {
				char msg[28];
				sprintf(msg, "Game is over, winner is %c\r\n", c);
				broadcast(msg, sizeof msg - 1);
				restart();
			}
			// Game is a draw
			else {
				static char msg[] = "Game is a draw\r\n";
				broadcast(msg, sizeof msg - 1);
				restart();
			}
		}
	}

	printf("shouldnt be here\n");



	
	return (status);
}

/*
---------------------------------------- Useful Helper Functions from usefulcode --------------------------------------------------
*/

void showboard(int fd)
{
    char buf[100], *bufp, *boardp;
    int col, row;
    // struct client *p; Going to use this struct later just commenting out for now so I don't get warning messages

    for (bufp = buf, col = 0, boardp = board; col < 3; col++) {
        for (row = 0; row < 3; row++, bufp += 4)
            sprintf(bufp, " %c |", *boardp++);
        bufp -= 2;  // kill last " |"
        strcpy(bufp, "\r\n---+---+---\r\n");
        bufp = strchr(bufp, '\0');
    }
    if (write(fd, buf, bufp - buf) != bufp-buf)
        perror("write");
}

int game_is_over()  /* returns winner, or ' ' for draw, or 0 for not over */
{
    int i, c;
    extern int allthree(int start, int offset);
    extern int isfull();

    for (i = 0; i < 3; i++)
        if ((c = allthree(i, 3)) || (c = allthree(i * 3, 1)))
            return(c);
    if ((c = allthree(0, 4)) || (c = allthree(2, 2)))
        return(c);
    if (isfull())
        return(' ');
    return(0);
}

int allthree(int start, int offset)
{
    if (board[start] > '9' && board[start] == board[start + offset]
            && board[start] == board[start + offset * 2])
        return(board[start]);
    return(0);
}

int isfull()
{
    int i;
    for (i = 0; i < 9; i++)
        if (board[i] < 'a')
            return(0);
    return(1);
}

// ------------------------------------ All these helper functions are from the muffinman.c helper class ---------------------------

void bindandlisten()  /* bind and listen, abort on error */
{
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	perror("socket");
	exit(1);
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
	perror("bind");
	exit(1);
    }

    if (listen(listenfd, 5)) {
	perror("listen");
	exit(1);
    }
}

void newconnection()  /* accept connection, sing to them, get response, update
                       * linked list */
{
    int fd;
    struct sockaddr_in r;
    socklen_t socklen = sizeof r;

    if ((fd = accept(listenfd, (struct sockaddr *)&r, &socklen)) < 0) {
	perror("accept");
    } else {
	printf("connection from %s\n", inet_ntoa(r.sin_addr));
	addclient(fd, r.sin_addr);
    }
}

static void addclient(int fd, struct in_addr addr)
{
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
	fprintf(stderr, "out of memory!\n");  /* highly unlikely to happen */
	exit(1);
    }
    printf("Adding client %s\n", inet_ntoa(addr));
    fflush(stdout);
    // Setting up struct for player
    p->buf = malloc(1000);
    p->bytes_in_buf = 0;
    p->fd = fd;
    p->ipaddr = addr;
    p->next = top;
    top = p;
    howmany++;
    showboard(p->fd); // Show board to the player
    p->role = "xo"[player_index]; // Assign player their role
    // If player has a role then tell them what role they are when they join
    if (player_index < 2) {
    	static char msg[21];
    	char msg2[22];
    	sprintf(msg, "Welcome, you are %c\r\n", "xo"[player_index]);
    	sprintf(msg2, "It is now %c's turns\r\n", "xo"[turn]);
    	write(p->fd, msg, sizeof msg - 1);
    	write(p->fd, msg2, sizeof msg2 - 1);
    	if (player_index == 0 && o_present) {
    		player_index = 3;
    	}
    	else {
    		if (player_index == 1)
    			o_present = 1;
    		player_index++;
    	}
    }
    // If they joined while 2 ppl are playing they just spectate
    else {
    	// If players are already set, this role 'w' will stand for waiting
    	p->role = 'w';
    	static char msg[] = "Welcome, others are playing but you may specate. If someone dcs you can pick up where they left off\r\n";
    	char msg2[22];
    	sprintf(msg2, "It is now %c's turns\r\n", "xo"[turn]);
    	write(p->fd, msg, sizeof msg - 1);
    	write(p->fd, msg2, sizeof msg2 - 1);
    }

}


static void removeclient(int fd)
{
    struct client **p;
    // Find client to remove
    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next);
    if (*p) {
    	// If client had a role, either assign a new role to someone else or hold till someone join
    	if ((*p)->role == 'x') {
    		struct client **q;
    		for (q = &top; *q && (*q)->role != 'w'; q = &(*q)->next);
    		// If there is a client ready then give them that new role and tell them
    		if (*q) {
    			printf("role is %c\n", (*q)->role);
    			(*q)->role = 'x';
    			static char msg[] = "You are now x\r\n";
    			write((*q)->fd, msg, sizeof msg - 1);
    		}
    		// If there isn't a client ready then hold the position of x for someone else to take
    		else {
    			player_index = 0;
    		}
    	}
    	if ((*p)->role == 'o') {
    		struct client **q;
    		for (q = &top; *q && (*q)->role != 'w'; q = &(*q)->next);
    		// If there is a client ready then give them that new role and tell them
    		if (*q) {
    			printf("role is %c\n", (*q)->role);
    			(*q)->role = 'o';
    			static char msg[] = "You are now o\r\n";
    			write((*q)->fd, msg, sizeof msg - 1);
    		}
    		// If there isn't a client ready then hold the position of x for someone else to take
    		else {
    			player_index = 1;
    		}
    	}



		struct client *t = (*p)->next;
		printf("Removing client %s\n", inet_ntoa((*p)->ipaddr));
		fflush(stdout);
		free(*p);
		*p = t;
		howmany--;
    } 
    else {
		fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n", fd);
		fflush(stderr);
    }
}


static void broadcast(char *s, int size)
{
    struct client *p;
    for (p = top; p; p = p->next)
	write(p->fd, s, size);
	/* should probably check write() return value and perhaps remove client */
}

static void makemove(char player, int move) {

	// Change board and change turn
	board[move - 1] = player;
	if (turn) {
		turn = 0;
	}
	else {
		turn = 1;
	}

	// Send message to everyone about board and turn
	char msg[24];
	char msg2[22];
	sprintf(msg, "Player %c made move %d\r\n", player, move);
	sprintf(msg2, "It is now %c's turns\r\n", "xo"[turn]);
	broadcast(msg, sizeof msg - 1);
	broadcast(msg2, sizeof msg2 - 1);
	struct client *p;
    for (p = top; p; p = p->next)
		showboard(p->fd);


}

// When game is over restart
void restart() {
	// Initializing board
	memcpy(board, "123456789", 9);
	// Reset turn
	turn = 0;
	struct client *p;

	for (p = top; p; p = p->next) {
		switch(p->role) {
			case'x':
				p->role = 'o';
				static char msg[] = "You are now o\r\n";
				write(p->fd, msg, sizeof msg - 1);
				showboard(p->fd);
				break;
			case'o':
				p->role = 'x';
				static char msg2[] = "You are now x\r\n";
				write(p->fd, msg2, sizeof msg2 - 1);
				showboard(p->fd);
				break;
			default: ;
				static char msg3[] = "Game is now restarting, x and o swapping\r\n";
				write(p->fd, msg3, sizeof msg3);
				break;
		}
	}
}
