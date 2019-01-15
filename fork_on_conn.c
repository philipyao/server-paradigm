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

    void handle_statistic(int);
    void handle_child_exit(int);
    signal(SIGINT, handle_statistic);
    signal(SIGCHLD, handle_child_exit);

    void handle_conn(int, int, struct sockaddr_in* cli_addr);

    printf("start to accept\n");
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

void handle_statistic(int sig) {
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
                exit(0);
            }
            write(connfd, buff, n);
        }
    }

    //parent process
    close(connfd);
}
