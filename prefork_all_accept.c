#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include "util.h"


static int listen_port = 9527;
static pid_t *children = NULL;
static const int nchild = 20;

int main() {
    int listen_fd = tcp_safe_listen(listen_port);

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
    close(listen_fd);

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
    output_statistic();
    exit(0);
}



void do_job(int listen_fd) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    void handle_conn(int, struct sockaddr_in*);

    //每个子进程复制 listen_fd 描述符，所有这些 listen_fd 都指向同一个 file 结构 （file指向最终的socket结构）
    //file 结构维护一个引用计数，N个进程拥有 listen_fd 则引用计数是 N
    //所有进程都阻塞在 listen_fd 的 accept 系统调用上时，实际是在 socket结构的 so_timeo 成员上进入睡眠；当有新
    //连接过来时，所有进程都被唤醒，但只有最先运行的进程获得连接(从已完成三次握手的队列里取，队列大小取决于 backlog 大小)
    //这个问题称为惊群(thundering herd)，有一个连接过来时，所有进程(或线程)都被唤醒，会导致性能受损（进程越多越明显）
    //注意 多进程或线程同时 accept 有些内核不支持
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
    static char buff[512];
    for( ; ; ) {
        if ((n = read(connfd, buff, sizeof(buff))) == 0) {
            printf("children %d client %d closed by peer\n", getpid(), connfd);
            //此时connfd被客户端关闭，处于半连接状态，调用close，完成四次挥手的后两步
            close(connfd);
            //本连接处理完毕，退出 handle_conn，继续 accept 并服务其他连接
            return;
        }
        write(connfd, buff, n);
    }
}
