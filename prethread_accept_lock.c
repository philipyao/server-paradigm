#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <pthread.h>
#include "util.h"

static const int listen_port = 9527;
static const int nThread = 5;

static pthread_mutex_t mutex;

int main() {
    int listen_fd = tcp_safe_listen(listen_port);

    if (pthread_mutex_init(&mutex, NULL)) {
        printf("create mutex error");
        exit(1);
    }

    void handle_int(int);
    signal(SIGINT, handle_int);

    void* do_job(void *arg);
    
    for (int i = 0; i < nThread; ++i) {
        pthread_t thread;
        pthread_create(&thread, NULL, do_job, &listen_fd);
    }

    for( ; ; ) {
        pause();
    }
    return 0;
}

void handle_int(int sig) {
    output_statistic();
    exit(0);
}

void* do_job(void *arg) {
    int listen_fd = *(int *)arg;
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    void handle_conn(int connfd, struct sockaddr_in *cli_addr);

    for ( ; ; ) {
        pthread_mutex_lock(&mutex);
        int connfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &clilen);
        pthread_mutex_unlock(&mutex);
        if (connfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            printf("accept connection error: %d\n", errno);
            return NULL;
        }
        handle_conn(connfd, &cli_addr);
    }
    return NULL;
}
void handle_conn(int connfd, struct sockaddr_in *cli_addr) {
    ssize_t n;
    static char buff[512];
    printf("start serving connection %d...\n", connfd);
    for( ; ; ) {
        if ((n = read(connfd, buff, sizeof(buff))) == 0) {
            printf("client %d closed by peer\n", connfd);
            close(connfd);
            return;
        }
        write(connfd, buff, n);
    }
    return;
}
