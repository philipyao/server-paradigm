#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <pthread.h>
#include "util.h"

static int listen_port = 9527;

int main() {
    int listen_fd = tcp_safe_listen(listen_port);

    void handle_int(int);
    signal(SIGINT, handle_int);

    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    void handle_conn(int listen_fd, int connfd, struct sockaddr_in* cli_addr);
    
    for ( ; ; ) {
        int connfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            printf("accept connection error: %d\n", errno);
            exit(1);
        }
        handle_conn(listen_fd, connfd, &cli_addr);
    }
    return 0;
}

void handle_int(int sig) {
    output_statistic();
    exit(0);
}

void *start_routine(void *args) {
    int connfd = *(int *)args;
    ssize_t n;
    static char buff[512];
    printf("start serving connection %d...\n", connfd);
    for( ; ; ) {
        if ((n = read(connfd, buff, sizeof(buff))) == 0) {
            printf("client %d closed by peer\n", connfd);
            close(connfd);
            return NULL;
        }
        write(connfd, buff, n);
    }
    return NULL;
}

void handle_conn(int listen_fd, int connfd, struct sockaddr_in* cli_addr) {    
    pthread_t thread;
    int retval = pthread_create(&thread, NULL, start_routine, &connfd);
    if (retval) {
        printf("pthread_create error %d\n", retval);
        return;
    }
}
