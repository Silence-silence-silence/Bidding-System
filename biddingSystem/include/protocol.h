#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

enum msg_types {
    OK,
    LOGIN = 0x10,
    LOGOUT,
    EUSRLGDIN = 0x1a,
    EWRNGPWD,
    ANCREATE = 0x20,
    ANCLOSED = 0x22,
    ANLIST,
    ANWATCH,
    ANLEAVE,
    ANBID,
    ANUPDATE,
    EANFULL = 0x2b,
    EANNOTFOUND,
    EANDENIED,
    EBIDLOW,
    EINVALIDARG,
    USRLIST = 0x32,
    USRWINS,
    USRSALES,
    USRBLNC,
    ESERV = 0xff
};

// This is the struct describes the header of the PETR protocol messages
typedef struct {
    uint32_t msg_len; // this should include the null terminator
    uint8_t msg_type;
} petr_header;

// read a petr header for a socket_fd, and place content into memory
// return 0 upon success, -1 on error
int rd_msgheader(int socket_fd, petr_header *h);

// write petr header by h and the msgbuf to socket_fd
// if msg_len is 0, msgbuf is ignored
// return 0 upon success, -1 on error
int wr_msg(int socket_fd, petr_header *h, char *msgbuf);

#endif