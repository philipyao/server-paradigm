#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <signal.h>


#define SIZE_BACKLOG    10
static pid_t *children = NULL;
static const int nchild = 10;

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        printf("create listen fd error: %d\n", errno);
        exit(1);
    }
    struct sockaddr_in server, cli_addr;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
	server.sin_port = htons(9527);
	server.sin_addr.s_addr = INADDR_ANY;

    int ret = bind(listen_fd, (struct sockaddr *)&server, sizeof(server));
    if (ret == -1) {
        printf("bind listen fd error: %d\n", errno);
        exit(1);
    }
    ret = listen(listen_fd, SIZE_BACKLOG);
    if (ret == -1) {
        printf("listen fd error: %d\n", errno);
        exit(1);
    }

    void do_job(int);

    children = (pid_t*)calloc(nchild, sizeof(pid_t));

    //预先fork子进程，各个子进程同时 accept
    for (int i = 0; i < nchild; ++i) {
        pid_t cid;
        if ((cid = fork()) == 0) {
            do_job(listen_fd);
        } else {
            children[i] = cid;
        }
    }

    //子进程fork之后再设置信号处理函数
    void handle_int(int);
    signal(SIGINT, handle_int);

    for ( ; ; ) {
        pause();
    }

    return 0;
}

void handle_int(int sig) {
    printf("handle_int\n");

    //杀死所有子进程
    for (int i = 0; i < sizeof(children)/sizeof(children[0]); ++i) {
        kill(children[i], SIGTERM);
    }

    //wait for all children to terminate
    while (wait(NULL) > 0 ) {
        ;
    }
    void output_statistic();
    output_statistic();
    exit(0);
}

void output_statistic() {
    struct rusage master_usage, children_usage;
    int ret = getrusage(RUSAGE_SELF, &master_usage);
    if (ret < 0) {
        printf("getrusage error %d\n", errno);
        return;
    }
    ret = getrusage(RUSAGE_CHILDREN, &children_usage);
    if (ret < 0) {
        printf("getrusage error %d\n", errno);
        return;
    }
    printf("time: master(%ld.%d, %ld.%d), children(%ld.%d, %ld.%d)\n", 
            master_usage.ru_utime.tv_sec, master_usage.ru_utime.tv_usec, 
            master_usage.ru_stime.tv_sec, master_usage.ru_stime.tv_usec,
            children_usage.ru_utime.tv_sec, children_usage.ru_utime.tv_usec, 
            children_usage.ru_stime.tv_sec, children_usage.ru_stime.tv_usec);
    exit(0);
}

void do_job(int listen_fd) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    void handle_conn(int, struct sockaddr_in*);

    for ( ; ; ) {
        int connfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            printf("accept connection error: %d\n", errno);
            exit(1);
        }
        handle_conn(connfd, &cli_addr);
    }
}

void handle_conn(int connfd, struct sockaddr_in* cli_addr) {
    printf("children %d handle client %d(%s:%d)）\n", getpid(), 
            connfd, inet_ntoa(cli_addr->sin_addr), ntohs(cli_addr->sin_port));
    
    ssize_t n;
    char buff[512];
    for( ; ; ) {
        if ((n = read(connfd, buff, sizeof(buff))) == 0) {
            printf("client %d closed by peer, child %d exit\n", connfd, getpid());
            exit(0);
        }
        write(connfd, buff, n);
    }
}
