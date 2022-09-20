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
#include "linkedlist.h"
#include "protocol.h"

#define SERVER_USAGE "usage: ./bin/zbid_server [-h] [-j N] [-t M] PORT_NUMBER AUCTION_FILENAME"
#define SA struct sockaddr
#define BUFFER_SIZE 1024


// global variable
int job_num = 2;
int wait_time = 0;
int PORT_NUMBER = -1;
char *AUCTION_FILENAME = NULL;
int listen_fd;
char buffer[BUFFER_SIZE];
pthread_mutex_t job_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t job_queue_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t auctions_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t user_mutex = PTHREAD_MUTEX_INITIALIZER;
linkedlist_t *work_threads = NULL;
linkedlist_t *users = NULL;
linkedlist_t *auctions = NULL;
linkedlist_t *job_queue = NULL;
int auction_id = 0;


typedef struct user_info {
    char *uname;
    char *pwd;
    void *client_fd;
} user_t;

/*
 * dealloc all global variable: job_queue, users, auctions, work_threads, and their internal dynamic alloc data
 */ 
void destroyAll() {
    // destroy job_queue ----------------------------------------
    job_t *job_head = (job_t*) job_queue->head;
    job_t *job_tmp = job_head;
    while (job_head != NULL) {
        free(job_head->msg);
        pthread_mutex_destroy(job_head->lock);
        free(job_head->lock);
        job_head = job_head->next;
        free(job_tmp);
        job_tmp = job_head;
    }
    free(job_queue);

    // destroy users linkedlist ----------------------------------------
    node_u *user_head = (node_u*) users->head;
    node_u *user_tmp = user_head;
    node_s *sale_head, *sale_tmp;
    node_w *win_head, *win_tmp;

    while (user_head != NULL) {
        free(user_head->uname);
        free(user_head->pwd);
        // free user's wins
        win_head = user_head->wins->head;
        win_tmp = win_head;
        while (win_head != NULL) {
            free(win_head->item_name);
            win_head = win_head->next;
            free(win_tmp);
            win_tmp = win_head;
        }
        // free user's sales
        sale_head = user_head->sales->head;
        sale_tmp = sale_head;
        while (sale_head != NULL) {
            free(sale_head->item_name);
            free(sale_head->winning_user);
            sale_head = sale_head->next;
            free(sale_tmp);
            sale_tmp = sale_head;
        }

        user_head = user_head->next;
        free(user_tmp);
        user_tmp = user_head;
    }
    free(users);

    // destroy auctions ----------------------------------------
    node_a *auc_head = (node_a*)auctions->head;
    node_a *auc_tmp = auc_head;
    node_u *watcher_head, *watcher_tmp;
    while (auc_head != NULL) {
        pthread_mutex_destroy(auc_head->lock);
        free(auc_head->lock);
        if (auc_head->creator_uname != NULL)
            free(auc_head->creator_uname);
        free(auc_head->item_name);

        // free watcher list
        watcher_head = auc_head->watch_users->head;
        watcher_tmp = watcher_head;
        while (watcher_head != NULL) {
            free(watcher_head->pwd);
            free(watcher_head->uname);

            watcher_head = watcher_head->next;
            free(watcher_tmp);
            watcher_tmp = watcher_head;
        }

        auc_head = auc_head->next;
        free(auc_tmp);
        auc_tmp = auc_head;
    }

    // destroy work_threads ----------------------------------------
    node_p *work_head = work_threads->head;
    node_p *work_tmp = work_head;
    while (work_head != NULL) {
        work_head = work_head->next;
        free(work_tmp);
        work_tmp = work_head;
    }
    free(work_threads);

}

void sigint_handler(int sig) {
    printf("shutting down the server\n");
    pthread_mutex_lock(&job_queue_mutex);
    pthread_mutex_lock(&user_mutex);
    pthread_mutex_lock(&auctions_mutex);
    destroyAll();
    pthread_mutex_unlock(&job_queue_mutex);
    pthread_mutex_unlock(&user_mutex);
    pthread_mutex_unlock(&auctions_mutex);
    close(listen_fd);
    exit(EXIT_SUCCESS);
}

void *process_tick(void* unused) {
    if (wait_time == 0) { // debug mode, waiting for stdin 
        while (1) {
            getchar();
            decreTickAuction();
        }
    } else { // real-time mode
        while (1) {
            sleep(wait_time);
            decreTickAuction();
        }
    }
    return NULL;
}

/*
 * init all shared data structures
 */ 
void *init_all() {
    printf("init job queue ....\n");
    //  init job queue
    job_queue = malloc(sizeof(linkedlist_t));
    job_queue->size = 0;
    job_queue->head = NULL;

    printf("init work thread....\n");
    // init working threads
    work_threads = malloc(sizeof(linkedlist_t));
    work_threads->size = 0;
    work_threads->head = NULL;
    int i;
    for (i=0; i<job_num; ++i) 
        insertIntoWorkingThreads(i);
    
    printf("init auction list....\n");
    // init auctions list
    auctions = malloc(sizeof(linkedlist_t));
    auctions->size = 0;
    auctions->head = NULL;

    printf("init users list....\n");
    // init users list
    users = malloc(sizeof(linkedlist_t));
    users->head = NULL;
    users->size = 0;
    
    printf("init tick threads....\n");
    // init tick thread
    pthread_t tid;
    void *unuse = NULL;
    pthread_create(&tid, NULL, process_tick, unuse);

    // init auctions in auction file
    printf("init auction file....\n");
    FILE *auction_file_ptr = fopen(AUCTION_FILENAME, "r");
    if (auction_file_ptr == NULL) {
        printf("No such file exists: %s\n", AUCTION_FILENAME);
        exit(EXIT_FAILURE);
    }
    int cycle, bin_price, count = 1;
    char msg[256], single_msg[64]; // cycless[16], bin_prices[32]
    while (fgets(single_msg, 64, auction_file_ptr) != NULL) {
        switch ((count++)%4) {
            case 1:
                single_msg[strcspn(single_msg, "\n")] = 0;
                strcpy(msg, "");
                strcat(msg, single_msg);
                strcat(msg, "\r\n");
                break;
            case 2:
                single_msg[strcspn(single_msg, "\n")] = 0;
                strcat(msg, single_msg);
                strcat(msg, "\r\n");
                break;
            case 3:
                single_msg[strcspn(single_msg, "\n")] = 0;
                strcat(msg, single_msg);
                break;
            case 0: ;
                char *item_name = malloc(sizeof(char)*256);
                int *duration = malloc(sizeof(int));
                float *bin_price = malloc(sizeof(float));
                create_auction(&item_name, duration, bin_price, msg, NULL);
                break;
        }
    }
    return NULL;
}


/*
 * @param int argc, char *argv[]
 * usage: ./bin/zbid_server [-h] [-j N] [-t M] PORT_NUMBER AUCTION_FILENAME
 * return 1 upon success except -h, 0 on error or -h
 */
int parseInput(int argc, char* argv[]) {
    int status, opterr = 0;
    while ((status = getopt(argc, argv, "hj:t:")) != -1) {
        switch (status)
        {
        case 'h':
            printf("%s\n", SERVER_USAGE);
            exit(EXIT_SUCCESS);
        case 'j':
            if (optarg != NULL) job_num = atoi(optarg);
            else {
                printf("%s\n", SERVER_USAGE);
                return 0;
            }
            break;
        case 't':
            if (optarg != NULL) wait_time = atoi(optarg);
            else {
                printf("%s\n", SERVER_USAGE);
                return 0;
            }
            break;
        default:
            printf("Unknown option %c, %s\n", status, SERVER_USAGE);
            exit(EXIT_FAILURE);
            break;
        }
    }

    int idx, flag=0;
    for (idx=optind; idx<argc; ++idx) {
        if (flag == 0) {
            PORT_NUMBER = atoi(argv[idx]);
            flag++;
        } else if (flag == 1) {
            AUCTION_FILENAME = argv[idx];
        }
    }

    if (PORT_NUMBER == -1) {
        printf("Missing Port Number\n");
        printf("%s\n", SERVER_USAGE);
        return 0;
    } else if (AUCTION_FILENAME == NULL) {
        printf("Missing Auction Filename\n");
        printf("%s\n", SERVER_USAGE);
        return 0;
    }
    
    return 1;
}

/*
 * code from lab 8
 */ 
int server_init(int server_port) {
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    }

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt))<0) {
	    perror("setsockopt");exit(EXIT_FAILURE);
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Currently listening on port %d.\n", server_port);

    return sockfd;
}


void *process_client(void* user) {
    void *clientfd_ptr = ((user_t*) user)->client_fd;
    char *uname = ((user_t*) user)->uname;
    char *pwd = ((user_t*) user)->pwd;
    int client_fd = *(int *)clientfd_ptr;
    free(clientfd_ptr);
    int received_size;
    fd_set read_fds;
    petr_header *header = malloc(sizeof(petr_header));
    int retval;
    while(1) {
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        retval = select(client_fd + 1, &read_fds, NULL, NULL, NULL);
        if (retval!=1 && !FD_ISSET(client_fd, &read_fds)){
            printf("Error with select() function\n");
            break;
        }

        received_size = rd_msgheader(client_fd, header);
        if(received_size < 0){
            logoutUser(uname);
            header->msg_type = OK;
            header->msg_len = 0;
            wr_msg(client_fd, header, "");
            break;
        }
        
        received_size = read(client_fd, buffer, header->msg_len);
        if(received_size < 0){
            printf("Receiving failed\n");
            break;
        }

        job_t *job = malloc(sizeof(job_t));
        pthread_mutex_t *theMutex = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(theMutex, NULL);
        job->lock = theMutex;
        job->client_fd = client_fd;
        job->logout = 0;
        job->msg_len = header->msg_len;
        job->msg_type = header->msg_type;
        job->pwd = pwd;
        job->uname = uname;
        job->msg = strdup(buffer);
        job->next = NULL;
        pthread_mutex_lock(theMutex);
        insertIntoJobQueue(job);
        pthread_mutex_lock(theMutex); // wait until after job is executed
        pthread_mutex_unlock(theMutex);
        pthread_mutex_destroy(theMutex);
        free(theMutex);
        if (job->logout) {
            free(job->msg);
            free(job);
            break;
        }

        free(job->msg);
        free(job);
    }
    // Close the socket at the end
    printf("Close current client connection\n");
    close(client_fd);
    free(header);
    return NULL;
}


int checkLogin(int client_fd, char **uname, char **pwd) {
    petr_header *header = malloc(sizeof(petr_header));
    int received_size, status_code;
    // error checking
    if ((received_size = rd_msgheader(client_fd, header)) == -1)
        exit(EXIT_FAILURE);
    else if (header->msg_type != 0x10) {
        printf("Not Login Request\n");
        exit(EXIT_FAILURE);
    }

    // error checking 
    if ((received_size = read(client_fd, buffer, header->msg_len)) < 0)
        exit(EXIT_FAILURE);
    else {
        if (sscanf(buffer, "%s\r\n%s", *uname, *pwd) != 2) {
            printf("Error when matchinng username and password\n");
            return 0;
        }
        node_u *user = malloc(sizeof(node_u));
        status_code = checkAccountStatus(*uname, *pwd);
        header->msg_len = 0;
        switch (status_code) {
            case 0:
                user->uname = strdup(*uname);
                user->pwd = strdup(*pwd);
                user->in_use = 1;
                user->next = NULL;
                user->client_fd = client_fd;
                user->sales = malloc(sizeof(linkedlist_t));
                user->wins = malloc(sizeof(linkedlist_t));
                user->sales->head = NULL;
                user->sales->size = 0;
                user->wins->head = NULL;
                user->wins->size = 0;
                header->msg_type = OK;
                insertIntoUsers(user);
                wr_msg(client_fd, header, "");
                break;
            case 1:
                header->msg_type = EUSRLGDIN;
                wr_msg(client_fd, header, "");
                break;
            case 2:
                header->msg_type = EWRNGPWD;
                wr_msg(client_fd, header, "");
                break;
            case 3:
                header->msg_type = OK;
                wr_msg(client_fd, header, "");
                break;
            default:
                printf("Unkown account status\n");
                break;
        }
    }

    free(header);
    return status_code;
}

/*
 * code from lab 8
 */ 
void run_server(int server_port) {
    listen_fd = server_init(server_port); // Initiate server and start listening on specified port
    int client_fd, status_code;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    init_all();
    pthread_t tid;
    if(signal(SIGINT, sigint_handler) == SIG_ERR)
	    printf("signal handler processing error\n");

    while(1){
        // Wait and Accept the connection from client
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(listen_fd, (SA*)&client_addr, &client_addr_len);
        if (*client_fd < 0) {
            printf("server acccept failed\n");
            exit(EXIT_FAILURE);
        }
        else {
            printf("accept connection\n");
            char *uname = malloc(sizeof(char)*256), *pwd = malloc(sizeof(char)*256);
            status_code = checkLogin(*client_fd, &uname, &pwd);
            if (status_code == 0 || status_code == 3) {
                user_t *new_user = malloc(sizeof(user_t));
                new_user->uname = strdup(uname);
                new_user->pwd = strdup(pwd);
                new_user->client_fd = (void *)client_fd;
                pthread_create(&tid, NULL, process_client, (void*) new_user);
            }
        }
    }
    close(listen_fd);
    return;
}