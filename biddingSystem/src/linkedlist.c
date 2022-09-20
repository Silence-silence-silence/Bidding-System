#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "protocol.h"
#include "linkedlist.h"
// ----------------------------------------------------------------------------
// data structure declaration


// ----------------------------------------------------------------------------
// auctions helper func

/*
 * @param node_a *auc
 * send ANCLOSED msg to all watcher
 */
void sendCloseMsg(node_a *auc) {
    petr_header *header = malloc(sizeof(petr_header));
    header->msg_type = ANCLOSED;
    char msg[256];
    pthread_mutex_lock(auc->lock);
    node_u *head = auc->watch_users->head;
    if (auc->has_been_bid) { // if has winner
        sprintf(msg, "%d\r\n%s\r\n%d", auc->auction_id, auc->highest_bidder, (int) (auc->bid*100));
    } else { // if don't have winner
        sprintf(msg, "%d\r\n\r\n", auc->auction_id);
    }
    header->msg_len = sizeof(char)*(strlen(msg)+1);

    while (head != NULL) {
        wr_msg(head->client_fd, header, msg);
        head = head->next;
    }
    pthread_mutex_unlock(auc->lock);
} 

/*
 * @param int client_fd (file descriptor of user)
 * send list to user
 */ 
void sendAnList(int client_fd) {
    node_a *head = auctions->head;
    int base = 1024, capacity = 1024;
    char *running_auctions = malloc(sizeof(char)*base);
    strcpy(running_auctions, "");
    while (head != NULL) {
        if (head->duration > 0) { // currently running auctions
            char *running_auction = malloc(sizeof(char)*256);
            sprintf(running_auction, "%d;%s;%d;%d;%d;%d\n", head->auction_id, head->item_name, (int) (head->bin_price*100), head->watcher_size, (int) (head->bid*100), head->duration);
            if (capacity-1 < strlen(running_auctions) + strlen(running_auction)) {
                capacity+=base;
                running_auctions = realloc(running_auctions, sizeof(char)*capacity);
            }
            if (strlen(running_auctions) > strlen(running_auction)) 
                running_auction = realloc(running_auction, sizeof(char)*(strlen(running_auctions)+1));
            strcat(running_auction, running_auctions);
            strcpy(running_auctions, running_auction);
            free(running_auction);
        }
        head = head->next;
    }

    petr_header *header = malloc(sizeof(petr_header));
    header->msg_type = ANLIST;
    if (strcmp(running_auctions, "") != 0) { // if there are currently running auctions
        strcat(running_auctions, "\0");
        header->msg_len = sizeof(char)*(strlen(running_auctions)+1);
        wr_msg(client_fd, header, running_auctions);
    } else {
        header->msg_len = 0;
        wr_msg(client_fd, header, "");
    }
    free(header);
    free(running_auctions);
}


void insertIntoAuctions(node_a *auction) {
    pthread_mutex_lock(&auctions_mutex);
    if (auctions->head == NULL) {
        auctions->head = auction;
    } else {
        node_a *head = (node_a*) auctions->head;
        auction->next = head;
        auctions->head = auction;
    }
    auctions->size++;
    pthread_mutex_unlock(&auctions_mutex);
}

/*
 * @param char **item_name, char **duration, char **bin_price, char *msg
 * 
 * return 1 if arguments are valid, return 0 on error
 * 
 */ 
int create_auction(char **item_name, int *duration, float *bin_price, char *msg, char *uname) {
    int count = 1;
    char *token = strtok(msg, "\r\n");
    while (token != NULL) {
        switch ((count++)%3) {
            case 1:
                *item_name = strdup(token);
                break;
            case 2:
                *duration = atoi(token);
                break;
            case 0:
                *bin_price = atof(token);
                break;
        }
        token = strtok(NULL, "\r\n");
    }

    if (count != 4 || *duration < 1 || *bin_price < 0 || strcmp(*item_name, "") == 0) {
        printf("Error Parsing Auction Create Message\n");
        return 0;
    } else {
        *bin_price/=100;
        *bin_price=( (float)( (int)( (*bin_price+0.005)*100 ) ) )/100; // round up to .00
        node_a *the_auction = malloc(sizeof(node_a));
        pthread_mutex_t *theMutex = malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(theMutex, NULL);
        the_auction->bid = 0.0;
        the_auction->sold = 0;
        the_auction->lock = theMutex;
        the_auction->highest_bidder = NULL;
        the_auction->auction_id = ++auction_id;
        the_auction->bin_price = *bin_price;
        if (uname != NULL)
            the_auction->creator_uname = strdup(uname);
        else
            the_auction->creator_uname = NULL;  
        the_auction->duration = *duration;
        the_auction->item_name = strdup(*item_name);
        the_auction->next = NULL;
        the_auction->has_been_bid = 0;
        the_auction->watch_users = 0;
        the_auction->watch_users = malloc(sizeof(linkedlist_t));
        the_auction->watch_users->size = 0;
        the_auction->watch_users->head = NULL;

        free(bin_price);
        free(duration);
        free(*item_name);
        insertIntoAuctions(the_auction);
        return 1;
    }
}

/*
 * remove watcher from particular auction 
 * 
 * @param char *uname, node_a *auc
 * 
 * return 1 if succeed, 0 if no such watcher
 */ 
int removeWatcherFromAuction(char *uname, node_a *auc) {
    pthread_mutex_lock(auc->lock);
    node_u *head = (node_u*) auc->watch_users->head;
    // if there is only zero or one watcher
    if (auc->watcher_size == 0) {
        pthread_mutex_unlock(auc->lock);
        return 0;
    } else if (auc->watcher_size == 1) {
        if (strcmp(head->uname, uname) == 0) {
            free(head->uname);
            free(head->pwd);
            free(head);
            auc->watch_users->head = NULL;
            auc->watcher_size--;
            pthread_mutex_unlock(auc->lock);
            return 1;
        }
        pthread_mutex_unlock(auc->lock);
        return 0;
    }

    // if there are 2 or more users
    node_u *prev = auc->watch_users->head;
    node_u *cur = prev->next;
    // first check if prev match 
    if (strcmp(prev->uname, uname) == 0) {
        auc->watch_users->head = cur;
        free(prev->uname);
        free(prev->pwd);
        free(prev);
        auc->watcher_size--;
        pthread_mutex_unlock(auc->lock);
        return 1;
    }

    while (cur != NULL) {
        if (strcmp(cur->uname, uname) == 0) {
            prev->next = cur->next;
            free(cur->uname);
            free(cur->pwd);
            free(cur);
            auc->watcher_size--;
            pthread_mutex_unlock(auc->lock);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(auc->lock);
    return 0;
}

void insertWatcherIntoAuction(char *uname, char *pwd, node_a *auc, int client_fd) {
    node_u *new_watcher = malloc(sizeof(node_u));
    new_watcher->next = NULL;
    new_watcher->pwd = strdup(pwd);
    new_watcher->uname = strdup(uname);
    new_watcher->in_use = 1;
    new_watcher->client_fd = client_fd;
    pthread_mutex_lock(auc->lock);
    node_u *head = (node_u*) auc->watch_users->head;
    if (head == NULL) {
        auc->watch_users->head = new_watcher;
    } else {
        new_watcher->next = head;
        auc->watch_users->head = new_watcher;
    }
    auc->watcher_size++;
    pthread_mutex_unlock(auc->lock);
}

void addWatcher(char *uname, char *pwd, int aid, int client_fd) {
    petr_header *header = malloc(sizeof(petr_header));
    pthread_mutex_lock(&auctions_mutex);
    node_a *head = (node_a*) auctions->head;
    while (head != NULL) {
        if (head->auction_id == aid) {
            insertWatcherIntoAuction(uname, pwd, head, client_fd);
            header->msg_type = ANWATCH;
            char response[256];
            sprintf(response, "%s\r\n%d", head->item_name, (int) (head->bin_price*100));
            header->msg_len = sizeof(char)*(strlen(response)+1);
            wr_msg(client_fd, header, response);
            free(header);
            pthread_mutex_unlock(&auctions_mutex);
            return;
        }
        head = head->next;
    }
    header->msg_len = 0;
    header->msg_type = EANNOTFOUND;
    wr_msg(client_fd, header, "");
    free(header);
    pthread_mutex_unlock(&auctions_mutex);
}

void removeWatcher(char *uname, int aid, int client_fd) {
    int res;
    petr_header *header = malloc(sizeof(petr_header));
    header->msg_len = 0;
    pthread_mutex_lock(&auctions_mutex);
    node_a *head = (node_a*) auctions->head;
    while (head != NULL) {
        if (head->auction_id == aid) {
            res = removeWatcherFromAuction(uname, head);
            break;
        }
        head = head->next;
    }
    if (res)  // if succeed
        header->msg_type = OK;
    else 
        header->msg_type = EANNOTFOUND;
    
    wr_msg(client_fd, header, "");
    free(header);
    pthread_mutex_unlock(&auctions_mutex);
}
/*
 * @param node_a *auc
 * notify all watcher current bif
 * 
 */ 
void notifyAllWatcher(node_a *auc) {
    char msg[256];
    petr_header *header = malloc(sizeof(petr_header));
    sprintf(msg, "%d\r\n%s\r\n%s\r\n%d", auc->auction_id, auc->item_name, auc->highest_bidder, (int) (auc->bid*100));
    header->msg_len = sizeof(char)*(strlen(msg)+1);
    header->msg_type = ANUPDATE;

    node_u *head = (node_u*) auc->watch_users->head;
    while (head != NULL) {
        wr_msg(head->client_fd, header, msg);
        head = head->next;
    }
    free(header);
}

/*
 * @param char *uname, node_a* auc
 * return 1 if exists and user is the owner, 0 if not
 */ 
int checkIfWatcherOrOwner(char *uname, node_a* auc) {
    if (auc->creator_uname != NULL && strcmp(auc->creator_uname, uname) == 0) 
        return 0;
    
    node_u *head = (node_u*) auc->watch_users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname) == 0) {
            return 1;
        }
        head = head->next;
    }
    return 0;
}

void updateBid(char* uname, char *msg, int client_fd) {
    int aid;
    float bid;
    petr_header *header = malloc(sizeof(petr_header));
    if (sscanf(msg, "%d\r\n%f", &aid, &bid) != 2) {
        header->msg_type = EANNOTFOUND;
        header->msg_len = 0;
        wr_msg(client_fd, header, "");
        free(header);
        return;
    }
    bid/=100;
    // check if auction_id exists and update bid
    pthread_mutex_lock(&auctions_mutex);
    node_a *head = auctions->head;
    while (head != NULL) {
        // if found that auction
        pthread_mutex_lock(head->lock);
        if (head->auction_id == aid) {
            // if the user is not watcher
            if (head->duration < 0) { // auction expired
                header->msg_len = 0;
                header->msg_type = EANNOTFOUND;
                wr_msg(client_fd, header, "");
                free(header);
                pthread_mutex_unlock(head->lock);
                pthread_mutex_unlock(&auctions_mutex);
                return;
            } else if (!checkIfWatcherOrOwner(uname, head)) {
                header->msg_len = 0;
                header->msg_type = EANDENIED;
                wr_msg(client_fd, header, "");
                free(header);
                pthread_mutex_unlock(head->lock);
                pthread_mutex_unlock(&auctions_mutex);
                return;
            } else { // if the user is the watcher
                if (bid >= head->bin_price && head->bin_price != 0) { // if reach bin_price
                    head->has_been_bid = 1;
                    head->bid = bid;
                    head->highest_bidder = strdup(uname);
                    head->sold = 1;
                    header->msg_type = OK;
                    header->msg_len = 0;
                    wr_msg(client_fd, header, "");
                    notifyAllWatcher(head);
                    insertIntoUserSales(head->creator_uname, head->auction_id, head->item_name, head->highest_bidder, head->bid);
                    insertIntoUserWins(head->highest_bidder, head->auction_id, head->item_name, head->bid);
                    pthread_mutex_unlock(head->lock);
                    sendCloseMsg(head);
                    free(header);
                    pthread_mutex_unlock(&auctions_mutex);
                    return;
                } else if (bid <= head->bid) { // if user bid lower than current bin_price
                    header->msg_len = 0;
                    header->msg_type = EBIDLOW;
                    wr_msg(client_fd, header, "");
                    free(header);
                    pthread_mutex_unlock(head->lock);
                    pthread_mutex_unlock(&auctions_mutex);
                    return;
                } else if (bid > head->bid){ // if user bid higher than current bid
                    head->has_been_bid = 1;
                    head->highest_bidder = strdup(uname);
                    head->bid = bid;
                    header->msg_type = OK;
                    header->msg_len = 0;
                    wr_msg(client_fd, header, "");
                    free(header);
                    notifyAllWatcher(head);
                    pthread_mutex_unlock(head->lock);
                    pthread_mutex_unlock(&auctions_mutex);
                    return;
                }
            }
        }
        pthread_mutex_unlock(head->lock);
        head = head->next;
    }

    pthread_mutex_unlock(&auctions_mutex);
    // if auction_id doesn't exist
    header->msg_len = 0;
    header->msg_type = EANNOTFOUND;
    wr_msg(client_fd, header, "");
    free(header);
}

// ----------------------------------------------------------------------------
// job_queue helper func

void insertIntoJobQueue(job_t *job) {
    pthread_mutex_lock(&job_queue_mutex);
    if (job_queue->head == NULL) {
        job_queue->head = job;
    } else {
        job_t *head = (job_t*) job_queue->head;
        job->next = head;
        job_queue->head = job;
    }
    job_queue->size++;
    pthread_cond_signal(&job_queue_cond);
    pthread_mutex_unlock(&job_queue_mutex);
}
/*
 * get and remove head of job_queue according to FIFO (first-in-first-out) policy
 * 
 * return NULL if there is no job 
 */ 
job_t* getNextJob() {
    job_t *job = (job_t*) job_queue->head;
    if (job != NULL) {
        job_queue->head = job->next;
        job_queue->size--;
    }
    return job;
}

// ----------------------------------------------------------------------------
// users helper func

void getUserWins(char *uname, int client_fd) {
    int base = 1024, capacity = 1024;
    char *msg = malloc(sizeof(char)*base), single_msg[256];
    petr_header *header = malloc(sizeof(petr_header));
    header->msg_type = USRWINS;
    strcpy(msg, "");
    pthread_mutex_lock(&user_mutex);
    node_u *head = (node_u*) users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname) == 0) {
            if (head->wins->size == 0) { // no wins
                header->msg_len = 0;
                wr_msg(client_fd, header, "");
                free(header);
                free(msg);
                pthread_mutex_unlock(&user_mutex);
                return;
            } else {
                node_w *win_head = (node_w*) head->wins->head;
                while (win_head != NULL) {
                    sprintf(single_msg, "%d;%s;%d\n", win_head->aid, win_head->item_name, (int) (win_head->winning_bid*100));
                    if (capacity-1 < strlen(msg) + strlen(single_msg)) {
                        capacity+=base;
                        msg = realloc(msg, capacity);
                    }
                    strcat(msg, single_msg);
                    win_head = win_head->next;
                }
                strcat(msg, "\0");
                header->msg_len = sizeof(char)*(strlen(msg)+1);
                wr_msg(client_fd, header, msg);
                free(header);
                free(msg);
                pthread_mutex_unlock(&user_mutex);
                return;
            }
        }
        head = head->next;
    }
    // TODO if no such user found
    free(header);
    free(msg);
    pthread_mutex_unlock(&user_mutex);
}

void getUserSales(char *uname, int client_fd) {
    int base = 1024, capacity = 1024;
    char *msg = malloc(sizeof(char)*base), single_msg[256];
    petr_header *header = malloc(sizeof(petr_header));
    header->msg_type = USRSALES;
    strcpy(msg, "");
    pthread_mutex_lock(&user_mutex);
    node_u *head = (node_u*) users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname) == 0) {
            if (head->sales->size == 0) { // no sales
                header->msg_len = 0;
                wr_msg(client_fd, header, "");
                free(header);
                free(msg);
                pthread_mutex_unlock(&user_mutex);
                return;
            } else {
                node_s *sale_head = (node_s*) head->sales->head;
                while (sale_head != NULL) {
                    sprintf(single_msg, "%d;%s;%s;%d\n", sale_head->aid, sale_head->item_name, sale_head->winning_user, (int) (sale_head->winning_bid*100));
                    if (capacity-1 < strlen(msg) + strlen(single_msg)) {
                        capacity+=base;
                        msg = realloc(msg, capacity);
                    }
                    strcat(msg, single_msg);
                    sale_head = sale_head->next;
                }
                strcat(msg, "\0");
                header->msg_len = sizeof(char)*(strlen(msg)+1);
                wr_msg(client_fd, header, msg);
                free(header);
                free(msg);
                pthread_mutex_unlock(&user_mutex);
                return;
            }
        }
        head = head->next;
    }
    // TODO if no such user found
    free(header);
    free(msg);
    pthread_mutex_unlock(&user_mutex);
}

void getUserBalance(char *uname, int client_fd) {
    petr_header *header = malloc(sizeof(petr_header));
    header->msg_type = USRBLNC;
    char msg[16];
    pthread_mutex_lock(&user_mutex);
    node_u *head = (node_u*) users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname) == 0) {
            sprintf(msg, "%d", (int) (head->balance*100));
            header->msg_len = sizeof(char)*(strlen(msg)+1);
            wr_msg(client_fd, header, msg);
            free(header);
            pthread_mutex_unlock(&user_mutex);
            return;
        }
        head = head->next;
    }
    free(header);
    pthread_mutex_unlock(&user_mutex);
}

/*
 * @param char *uname, int aid, char *item_name, float winning_bid
 * 
 * insert wins into user node
 */ 
void insertIntoUserWins(char *uname, int aid, char *item_name, float winning_bid) {
    node_w *win = malloc(sizeof(node_w));
    win->aid = aid;
    win->item_name = strdup(item_name);
    win->winning_bid = winning_bid;
    win->next = NULL;

    pthread_mutex_lock(&user_mutex);
    node_u *head = (node_u*) users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname) == 0) {
            node_w *win_head = (node_w*) head->wins->head;
            if (win_head == NULL) {
                head->wins->head = win;
            } else {
                win->next = win_head;
                head->wins->head = win;
            }
            head->balance-=winning_bid;
            head->wins->size++;
            pthread_mutex_unlock(&user_mutex);
            return;
        }
        head = head->next;
    }

    // TODO if no such user found, probably don't need to handle in the version of client
    pthread_mutex_unlock(&user_mutex);
}

/*
 * @param char *uname, int aid, char *item_name, char* winning_user, float winning_bid
 * 
 * insert sales into user node
 */ 
void insertIntoUserSales(char *uname, int aid, char *item_name, char* winning_user, float winning_bid) {
    if (uname == NULL) 
        return;
    node_s *sale = malloc(sizeof(node_s));
    sale->aid = aid;
    sale->item_name = strdup(item_name);
    sale->winning_bid = winning_bid;
    sale->winning_user = strdup(winning_user);
    sale->next = NULL;
    pthread_mutex_lock(&user_mutex);
    node_u *head = (node_u*) users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname) == 0) {
            node_s *sale_head = (node_s*) head->sales->head;
            if (sale_head == NULL) {
                head->sales->head = sale;
            } else {
                sale->next = sale_head;
                head->sales->head = sale;
            }
            head->balance+=winning_bid;
            head->sales->size++;
            pthread_mutex_unlock(&user_mutex);
            return;
        }
        head = head->next;
    }
    pthread_mutex_unlock(&user_mutex);
}

/*
 * @param char *uname, int client_fd
 * 
 * send a list of active users to requesting user (uname)
 * 
 */
void sendActiveUsers(char *uname, int client_fd) {
    petr_header *header = malloc(sizeof(petr_header));
    node_u *head = (node_u*) users->head;
    int base=1024, capacity=1024, has = 0;
    char *msg = malloc(sizeof(char)*base), name[64];
    strcpy(msg, "");
    header->msg_type = USRLIST;

    while (head != NULL) {
        if (head->in_use && strcmp(head->uname, uname) != 0) { // if user logged in, and not requesting user
            has = 1;
            sprintf(name, "%s\n", head->uname);
            if (capacity-1 < strlen(msg)+strlen(name)) {
                capacity+=base;
                msg = realloc(msg, capacity);
            }
            strcat(msg, name);
        }
        head = head->next;
    }
    strcat(msg, "\0");

    if (!has) { // if having other logged in user
        header->msg_len = 0;
        wr_msg(client_fd, header, "");
    } else {
        header->msg_len = sizeof(char)*(strlen(msg)+1);
        wr_msg(client_fd, header, msg);
    }
    free(msg);
    free(header);
}

void insertIntoUsers(node_u *node) {
    if (users->head == NULL) {
        users->head = node;
    } else {
        node_u *head = (node_u*) users->head;
        node->next = head;
        users->head = node;
    }
    users->size++;
}

void logoutUser(char *uname) {
    node_u *head = (node_u*) users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname)==0) {
            head->in_use = 0;
            return;
        }
        head = head->next;
    }
}

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
int checkAccountStatus(char* uname, char* pwd) {
    node_u *head = (node_u*) users->head;
    while (head != NULL) {
        if (strcmp(head->uname, uname) == 0) {
            if (head->in_use) 
                return 1;
            if (strcmp(head->pwd, pwd) == 0) 
                return 3;
            else 
                return 2;
        }
        head = head->next;
    }
    return 0;
}


// ----------------------------------------------------------------------------
//  helper function for working threads

void* work_process(void *work_thread) {
    node_p *the_thread = (node_p*) work_thread;
    while (1) { // never terminate
        pthread_mutex_lock(&job_queue_mutex);
        while (job_queue->size == 0) // waiting if there isn't any jobs
            pthread_cond_wait(&job_queue_cond, &job_queue_mutex);
        job_t *job = getNextJob();
        pthread_mutex_unlock(&job_queue_mutex);
        if (job == NULL) 
            continue;
        the_thread->work_status = 1;
        petr_header *header = malloc(sizeof(petr_header));
        int aid;
        switch (job->msg_type) {
            case LOGOUT:
                logoutUser(job->uname);
                header->msg_type = OK;
                header->msg_len = 0;
                wr_msg(job->client_fd, header, "");
                job->logout = 1;
                pthread_mutex_unlock(job->lock);
                break;
            case ANCREATE: ;
                char *item_name = malloc(sizeof(char)*256);
                int *duration = malloc(sizeof(int));
                float *bin_price = malloc(sizeof(float));
                if (create_auction(&item_name, duration, bin_price, job->msg, job->uname)) {
                    header->msg_type = ANCREATE;
                    char string_auction_id[32];
                    sprintf(string_auction_id, "%d", auction_id);
                    header->msg_len = sizeof(char)*(strlen(string_auction_id)+1);
                    wr_msg(job->client_fd, header, string_auction_id);
                } else {
                    free(item_name);
                    free(duration);
                    free(bin_price);
                    header->msg_type = EINVALIDARG;
                    header->msg_len = 0;
                    wr_msg(job->client_fd, header, "");
                }
                pthread_mutex_unlock(job->lock);
                break;
            case ANLIST:
                sendAnList(job->client_fd);
                pthread_mutex_unlock(job->lock);
                break;
            case ANWATCH: ;
                if (sscanf(job->msg, "%d",  &aid) != 1) {
                    header->msg_type = EANNOTFOUND;
                    header->msg_len = 0;
                    wr_msg(job->client_fd, header, "");
                } else {
                    addWatcher(job->uname, job->pwd, aid, job->client_fd);
                }
                pthread_mutex_unlock(job->lock);
                break;
            case ANLEAVE: ;
                if (sscanf(job->msg, "%d",  &aid) != 1) {
                    header->msg_type = EANNOTFOUND;
                    header->msg_len = 0;
                    wr_msg(job->client_fd, header, "");
                } else {
                    removeWatcher(job->uname, aid, job->client_fd);
                }
                pthread_mutex_unlock(job->lock);
                break;
            case ANBID:  
                updateBid(job->uname, job->msg, job->client_fd);
                pthread_mutex_unlock(job->lock);
                break;
            case USRLIST:
                sendActiveUsers(job->uname, job->client_fd);
                pthread_mutex_unlock(job->lock);
                break;
            case USRWINS:
                getUserWins(job->uname, job->client_fd);
                pthread_mutex_unlock(job->lock);
                break;
            case USRSALES:
                getUserSales(job->uname, job->client_fd);
                pthread_mutex_unlock(job->lock);
                break;
            case USRBLNC:
                getUserBalance(job->uname, job->client_fd);
                pthread_mutex_unlock(job->lock);
                break;
            case ANCLOSED:
                pthread_mutex_unlock(job->lock);
                break;
            default:
                printf("Unknown msg type: %d\n", job->msg_type);
                break;
        }
        free(header);
        the_thread->work_status = 0;
    }
    return NULL;
}

void insertIntoWorkingThreads(int id) {
    // init a new work thread
    node_p *work_thread = malloc(sizeof(node_p));
    pthread_t tid;
    work_thread->id = id;
    work_thread->next = NULL;
    work_thread->work_status = 0;
    pthread_create(&tid, NULL, work_process, (void*) work_thread);
    work_thread->tid = tid;

    if (work_threads->head == NULL) {
        work_threads->head = work_thread;
    } else {
        node_p *head = (node_p*) work_threads->head;
        work_threads->head = work_thread;
        ((node_p*) (work_threads->head))->next = (void*) head;
    }
    work_threads->size++;
}

/*
 * get next Available idle working threads
 * 
 * return pointer to free working thread if there is one, otherwise return NULL.
 * 
 */ 
node_p *getNextAvailableWorkingthread() {
    node_p *head = (node_p*) work_threads->head;
    while (head != NULL) {
        if (!head->work_status) 
            return head;
        head = head->next;
    }
    return head;
}
// ----------------------------------------------------------------------------

/*
 * decre tick time
 */ 
void decreTickAuction() {
    pthread_mutex_lock(&auctions_mutex);
    node_a *head = auctions->head;
    while (head != NULL) {
        head->duration--;
        if (head->duration <= 0) {
            if (head->has_been_bid && !head->sold) { // if there is someone bids and not sold
                
                insertIntoUserSales(head->creator_uname, head->auction_id, head->item_name, head->highest_bidder, head->bid);
                insertIntoUserWins(head->highest_bidder, head->auction_id, head->item_name, head->bid);
                sendCloseMsg(head);
                head->sold = 1;
            }
        }
        head = head->next;
    }
    pthread_mutex_unlock(&auctions_mutex);
}
