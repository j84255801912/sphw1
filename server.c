#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    int id;
    int money;
} Account;

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
	int account;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";
const char* reject_header = "REJECT\n";

// Forwards

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error

int main(int argc, char** argv) {
    int i, ret, j;

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    int fd = open("account_info", O_RDWR|O_CREAT);
    int mine_max_fd = svr.listen_fd, result;
    fd_set the_set, temp_set, write_set;
    FD_ZERO(&the_set);
    FD_SET(svr.listen_fd, &the_set);
    while (1) {
        memcpy(&temp_set, &the_set, sizeof(temp_set));
        clilen = sizeof(cliaddr);
        result = select(mine_max_fd+1, &temp_set, NULL, NULL, NULL);
        if (result > 0) {
            if (FD_ISSET(svr.listen_fd, &temp_set)) {
                conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
                if (conn_fd < 0) {
                    fprintf(stderr, "error in accept\n");
                    continue;
                }
                requestP[conn_fd].conn_fd = conn_fd;
                strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
                FD_SET(conn_fd, &the_set);
                mine_max_fd = (mine_max_fd < conn_fd) ? conn_fd : mine_max_fd;
                continue;
            }
            for (i = 0; i < maxfd; i++) {
                if (requestP[i].conn_fd != -1) {
                    if (FD_ISSET(requestP[i].conn_fd, &temp_set)) {
                        ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf
                        if (ret < 0) {
                            fprintf(stderr, "bad request from %s\n", requestP[i].host);
                            continue;
                        }
                    //    sprintf(buf,"%s : %s\n",accept_read_header,requestP[i].buf);
                    //    write(requestP[i].conn_fd, buf, strlen(buf));
                        #ifdef READ_SERVER
                        char *end;
                        int acc = (int)strtol(requestP[i].buf, &end, 10);
                        if (*end != 0) {
                            sprintf(buf,"please enter account number only! : %s\n", requestP[i].buf);
                            write(requestP[i].conn_fd, buf, strlen(buf));
                            continue;
                        }
                        struct flock lock;
                        memset(&lock, 0, sizeof(lock));
                        lock.l_type     = F_RDLCK;
                        lock.l_whence   = SEEK_SET;
                        lock.l_start    = acc*sizeof(Account);
                        lock.l_len      = sizeof(Account);

                        int test = fcntl(fd, F_SETLK, &lock);
                        if (test < 0) {
                            sprintf(buf, "This account is occupied.\n");
                            write(requestP[i].conn_fd, buf, strlen(buf));
                        } else {
                            Account temp;
                            lseek(fd, acc*sizeof(Account), SEEK_SET);
                            int bytes = read(fd, &temp, sizeof(Account));
                            if (bytes > 0) {
                                sprintf(buf, "Balance: %d\n", temp.money);
                                write(requestP[i].conn_fd, buf, strlen(buf));
                            }
                            lock.l_type = F_UNLCK;
                            fcntl(fd, F_SETLK, &lock);
                        }
//                        close(fd);

                        #else

                        if (requestP[i].buf[0] == '+' || requestP[i].buf[0] == '-') {
                            char *end;
                            int value = (int)strtol(requestP[i].buf, &end, 10);
                            /*
                            if (*end != 0) {
                                sprintf(buf,"please enter account number only! : %s\n", requestP[i].buf);
                                write(requestP[i].conn_fd, buf, strlen(buf));
                                continue;
                            }
                            */
//                            int fd = open("account_info", O_RDWR|O_CREAT);
                            int acc = requestP[i].account;
                            Account temp;
                            lseek(fd, acc*sizeof(Account), SEEK_SET);
                            int bytes = read(fd, &temp, sizeof(Account));
                            if (bytes > 0) {
                                /*
                                sprintf(buf, "Balance: %d\n", temp.money);
                                write(requestP[i].conn_fd, buf, strlen(buf));
                                */
                                if (temp.money + value < 0) {
                                    sprintf(buf, "Operation fail.\n");
                                    write(requestP[i].conn_fd, buf, strlen(buf));
                                } else {
                                    // write new value into the file.
                                    temp.money = temp.money + value;
                                    lseek(fd, acc*sizeof(Account), SEEK_SET);
                                    write(fd, &temp, sizeof(Account));
                                }
                            }

                            struct flock lock;
                            memset(&lock, 0, sizeof(lock));
                            lock.l_type = F_UNLCK;
                            fcntl(fd, F_SETLK, &lock);
//                            close(fd);
                        } else {
                            char *end;
                            int acc = (int)strtol(requestP[i].buf, &end, 10);
                            /*
                            if (*end != 0) {
                                sprintf(buf,"please enter account number only! : %s\n", requestP[i].buf);
                                write(requestP[i].conn_fd, buf, strlen(buf));
                                continue;
                            }
                            */
//                            int fd = open("account_info", O_RDWR|O_CREAT);
                            requestP[i].account = acc;

                            // set the lock
                            struct flock lock;
                            memset(&lock, 0, sizeof(lock));
                            lock.l_type     = F_WRLCK;
                            lock.l_whence   = SEEK_SET;
                            lock.l_start    = acc*sizeof(Account);
                            lock.l_len      = sizeof(Account);

                            int test = fcntl(fd, F_SETLK, &lock);
                            if (test < 0) {
                                sprintf(buf, "This account is occupied.\n");
                                write(requestP[i].conn_fd, buf, strlen(buf));
                            } else {
                                int occupied_in = 0;
                                for (j = 0; j < maxfd; j++)
                                    if (i != j && requestP[j].account == requestP[i].account)
                                        occupied_in = 1;
                                if (!occupied_in) {
                                    sprintf(buf, "This account is available.\n");
                                    write(requestP[i].conn_fd, buf, strlen(buf));
                                    continue;
                                } else {
                                    sprintf(buf, "This account is occupied.\n");
                                    write(requestP[i].conn_fd, buf, strlen(buf));
                                }
                            }
                        }
                        #endif

		                close(requestP[i].conn_fd);
		                free_request(&requestP[i]);
                        FD_CLR(i, &the_set);
                    } // if (FD_ISSET(....
                } // if (requestP[i].conn_fd != -1) {
            } // for (i = 0; i < maxfd; i++) {
        } // if result > 0..
    }
    close(fd);
    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->account = -1;
    reqP->wait_for_write = 0;
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
	char* p1 = strstr(buf, "\015\012");
	int newline_len = 2;
	// be careful that in Windows, line ends with \015\012
	if (p1 == NULL) {
		p1 = strstr(buf, "\012");
		newline_len = 1;
		if (p1 == NULL) {
			ERR_EXIT("this really should not happen...");
		}
	}
	size_t len = p1 - buf + 1;
	memmove(reqP->buf, buf, len);
	reqP->buf[len - 1] = '\0';
	reqP->buf_len = len-1;
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}

