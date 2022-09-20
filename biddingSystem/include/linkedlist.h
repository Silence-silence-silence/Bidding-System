#ifndef LINKEDLIST_H
#define LINKEDLIST_H
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct linkedlist {
    void *head;
    size_t size;
} linkedlist_t;

typedef struct node_win {
    int aid;
    float winning_bid;
    char *item_name;
    struct node_win *next;
} node_w;

typedef struct node_sales {
    int aid;
    float winning_bid;
    char *item_name;
    char *winning_user;
    struct node_sales *next;
} node_s;


typedef struct node_pthread {
    pthread_t tid;
    int work_status;
    int id;
    struct node_pthread *next;
} node_p;

typedef struct node_user {
    char *uname;
    char *pwd;
    int in_use;
    int client_fd;
    float balance;
    linkedlist_t *sales;
    linkedlist_t *wins;
    struct node_user *next;
} node_u;

typedef struct node_auction {
    int auction_id;
    int sold;
    char *creator_uname;
    char *highest_bidder;
    char *item_name;
    int duration;
    int has_been_bid;
    float bin_price;
    float bid;
    pthread_mutex_t *lock;
    linkedlist_t *watch_users;
    int watcher_size;
    struct node_auction *next;
} node_a;

typedef struct job_info {
    char *uname;
    char *pwd;
    int client_fd;
    int logout;
    pthread_mutex_t *lock;
    uint32_t msg_len;
    uint8_t msg_type;
    char *msg;
    struct job_info *next;
} job_t;

extern linkedlist_t *work_threads;
extern linkedlist_t *users;
extern linkedlist_t *job_queue;
extern linkedlist_t *auctions;
extern pthread_mutex_t job_queue_mutex;
extern pthread_cond_t job_queue_cond;
extern pthread_mutex_t user_mutex;
extern pthread_mutex_t auctions_mutex;
extern int auction_id;
void insertIntoUserSales(char *uname, int aid, char *item_name, char* winning_user, float winning_bid);
void insertIntoUserWins(char *uname, int aid, char *item_name, float winning_bid);
/*
 * @param node_a *auc
 * send ANCLOSED msg to all watcher
 */
void sendCloseMsg(node_a *auc);
/*
 * @param int client_fd (file descriptor of user)
 * send list to user
 */ 
void sendAnList(int client_fd);
void insertIntoAuctions(node_a *auction);

/*
 * @param char **item_name, char **duration, char **bin_price, char *msg
 * 
 * return 1 if arguments are valid, return 0 on error
 * 
 */ 
int create_auction(char **item_name, int *duration, float *bin_price, char *msg, char *uname);

/*
 * remove watcher from particular auction 
 * 
 * @param char *uname, node_a *auc
 * 
 * return 1 if succeed, 0 if no such watcher
 */ 
int removeWatcherFromAuction(char *uname, node_a *auc);

void insertWatcherIntoAuction(char *uname, char *pwd, node_a *auc, int client_fd);
void addWatcher(char *uname, char *pwd, int aid, int client_fd);
void removeWatcher(char *uname, int aid, int client_fd);
/*
 * @param node_a *auc
 * notify all watcher current bif
 * 
 */ 
void notifyAllWatcher(node_a *auc);
/*
 * @param char *uname, node_a* auc
 * return 1 if exists and user is the owner, 0 if not
 */ 
int checkIfWatcherOrOwner(char *uname, node_a* auc);
void updateBid(char* uname, char *msg, int client_fd);
void insertIntoJobQueue(job_t *job);
/*
 * get and remove head of job_queue according to FIFO (first-in-first-out) policy
 * 
 * return NULL if there is no job 
 */ 
job_t* getNextJob();
void getUserWins(char *uname, int client_fd);
void getUserSales(char *uname, int client_fd);
void getUserBalance(char *uname, int client_fd);
/*
 * @param char *uname, int aid, char *item_name, float winning_bid
 * 
 * insert wins into user node
 */ 
void insertIntoUserWins(char *uname, int aid, char *item_name, float winning_bid);
/*
 * @param char *uname, int aid, char *item_name, char* winning_user, float winning_bid
 * 
 * insert sales into user node
 */ 
void insertIntoUserSales(char *uname, int aid, char *item_name, char* winning_user, float winning_bid);

/*
 * @param char *uname, int client_fd
 * 
 * send a list of active users to requesting user (uname)
 * 
 */
void sendActiveUsers(char *uname, int client_fd);
void insertIntoUsers(node_u *node);
void logoutUser(char *uname);
/*
 * @param uname: user name
 * @param pwd: password
 * 
 * return 0 if no such user exists. 
 * return 1 if account is in use.
 * return 2 if incorrect password.
 * return 3 if username and password match.
 * 
 */
int checkAccountStatus(char* uname, char* pwd);
void* work_process(void *work_thread);
void insertIntoWorkingThreads(int id);
/*
 * get next Available idle working threads
 * 
 * return pointer to free working thread if there is one, otherwise return NULL.
 * 
 */ 
node_p *getNextAvailableWorkingthread();
void decreTickAuction();

#endif