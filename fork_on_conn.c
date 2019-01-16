#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include "util.h"

static int listen_port = 9527;

int main() {
    int listen_fd = tcp_safe_listen(listen_port);

    void handle_int(int);
    void handle_child_exit(int);
    signal(SIGINT, handle_int);
    signal(SIGCHLD, handle_child_exit);

    void handle_conn(int, int, struct sockaddr_in* cli_addr);

    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
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

void handle_child_exit(int sig) {
    printf("handle_child_exit\n");
}

void handle_int(int sig) {
    output_statistic();
    exit(0);
}

void handle_conn(int listen_fd, int connfd, struct sockaddr_in* cli_addr) {
    pid_t cid;
    if ((cid = fork()) == 0) {
        //child process
        close(listen_fd);
        printf("children %d handle client %d(%s:%d)）\n", getpid(), 
                connfd, inet_ntoa(cli_addr->sin_addr), ntohs(cli_addr->sin_port));
        
        ssize_t n;
        char buff[512];
        for( ; ; ) {
            if ((n = read(connfd, buff, sizeof(buff))) == 0) {
                printf("client %d closed by peer, child %d exit\n", connfd, getpid());
                close(connfd);
                exit(0);
            }
            write(connfd, buff, n);
        }
    }

    //parent process
    close(connfd);
}
