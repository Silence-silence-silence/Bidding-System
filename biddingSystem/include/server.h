#ifndef SERVER_H
#define SERVER_H

/*
 * @param int argc, char *argv[]
 * usage: ./bin/zbid_server [-h] [-j N] [-t M] PORT_NUMBER AUCTION_FILENAME
 * return 1 upon success except -h, 0 on error or -h
 */
int parseInput(int argc, char* argv[]);

/*
 * @param int server_port
 * 
 * running server on server_port, ctr-c to shutdown the server.
 */ 
void run_server(int server_port);

#endif
