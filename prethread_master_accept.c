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
static pthread_cond_t cond;

//主线程将连接放入环形队列，工作子线程从环形队列中取数据来处理
//所有线程间的同步是通过互斥锁和条件变量来进行
static const int kPendingMax = 4;
static int pending_conns[kPendingMax];
static int pending_head;
static int pending_tail;

void init_pending() {
    pending_head = pending_tail = 0;
}
void push_pending(int connfd) {
    pthread_mutex_lock(&mutex);

    //if pending queue is full, just wait util condition triggered by workers
    while ((pending_head + 1) % kPendingMax == pending_tail) {
        pthread_cond_wait(&cond, &mutex);
    }

    pending_conns[pending_head] = connfd;
    pending_head = (pending_head + 1) % kPendingMax;
    printf("push_pending: connfd %d, head %d\n", connfd, pending_head);
    //唤醒一个等待的子线程来处理（如果有多个，则只唤醒其中一个）
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

int pop_pending() {
    pthread_mutex_lock(&mutex);

    //if pending queue is empty, just wait util condition triggered by master
    while (pending_head == pending_tail) {
        pthread_cond_wait(&cond, &mutex);
    }
    int connfd = pending_conns[pending_tail];
    pending_tail = (pending_tail + 1) % kPendingMax;
    printf("pop_pending: connfd %d, tail %d\n", connfd, pending_tail);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    return connfd;
}

int main() {
    int listen_fd = tcp_safe_listen(listen_port);

    if (pthread_mutex_init(&mutex, NULL)) {
        printf("create mutex error\n");
        exit(1);
    }
    if (pthread_cond_init(&cond, NULL)) {
        printf("create cond error\n");
        exit(1);
    }
    init_pending();

    void* do_job(void *arg);
    pthread_t thread;
    for (int i = 0; i < nThread; ++i) {
        pthread_create(&thread, NULL, do_job, (void *)(long long)i);
    }

    void handle_int(int);
    signal(SIGINT, handle_int);

    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    for ( ; ; ) {
        int connfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            printf("accept connection error: %d\n", errno);
            break;
        }
        push_pending(connfd);
    }
    return 0;
}

void handle_int(int sig) {
    output_statistic();
    exit(0);
}

void* do_job(void *arg) {
    int idx = (int)(long long)arg;
    void handle_conn(int idx, int connfd);
    int connfd;
    for ( ; ; ) {
        printf("thread %d waiting for connection\n", idx);
        connfd = pop_pending();
        handle_conn(idx, connfd);
    }
    return NULL;
}
void handle_conn(int idx, int connfd) {
    ssize_t n;
    static char buff[512];
    printf("thread %d start serving connection %d...\n", idx, connfd);
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
