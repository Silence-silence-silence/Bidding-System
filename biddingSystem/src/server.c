#include "server.h"
#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>

extern int PORT_NUMBER;
int main(int argc, char* argv[]) {
    // first check argv
    if (!parseInput(argc, argv)) 
        exit(EXIT_FAILURE);

    // run server
    run_server(PORT_NUMBER);
    return 0;
}
