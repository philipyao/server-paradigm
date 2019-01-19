#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <pthread/pthread.h>
#include <sys/types.h>
#include <sys/select.h>
#include <assert.h>
#include "util.h"

static const int kListenPort = 9527;

struct Child {
    pid_t pid;
    int pipe_fd;
    //这里一个子进程一个只能服务一个连接，所以要标识忙闲
    //其实借助select/epoll多路复用技术完全可以服务多个连接
    int status;
};
typedef struct Child Child;

static Child *children = NULL;
static const int nchild = 5;

//派发客户端连接
void diapatch_conn(int connfd) {
    //找出一个空闲的子进程
    int i;
    for (i = 0; i < nchild; ++i) {
        if (children[i].status == 0) { break; }
    }
    if (i == nchild) {
        printf("no worker found for conn %d\n", connfd);
        return;
    }
    printf("diapatch connection %d to child %d pid %d\n", connfd, i, children[i].pid);
    children[i].status = 1;

    //使用unix域套接字传递套接字（客户端连接connfd）
    //注意不能直接直接往pipe_fd里写connfd，因为每个进程的fd只是文件描述表的索引
    //两个进程的文件描述表一般都不同（父子进程fork的之后相同，但如果父子进程又打开了描述符，则不同)
    //fd作为描述表的索引，在进程内才有意义，直接在进程间传递没有意义
    //关于进程间传递描述符，实际是重新创建了文件描述项，具体可以查看APUE或者UNP的相关章节
    //另外，除了父进程往子进程传递描述符让子进程来read，也有的服务器模型是在master父进程中read，然后
    //将read到的数据dispatch到子进程进行处理，这样实际的IO完全都在master做，子进程只是处理逻辑
    ssize_t retval = write_fd(children[i].pipe_fd, connfd);
    printf("write to child %d, pipe_fd %d, val %d, retval %zd\n", i, children[i].pipe_fd, connfd, retval);
}

void handle_listen(int listen_fd) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    //主进程负责accept新连接，然后交给子进程维护
    int connfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &clilen);
    if (connfd < 0) {
        if (errno == EINTR) {
            return;
        }
        printf("accept connection error: %d\n", errno);
        exit(1);
    }
    diapatch_conn(connfd);
}

void handle_child_msg(int idx) {
    children[idx].status = 0;
    printf("handle_child_msg: child %d pid %d available\n", idx, children[idx].pid);
}

void build_fd_set(int listen_fd, fd_set *readfds, int *maxfd) {
    assert(readfds != NULL && maxfd != NULL);

    FD_ZERO(readfds);
    FD_SET(listen_fd, readfds);
    *maxfd = listen_fd;
    for (int i = 0; i < nchild; ++i) {
        if (children[i].status == 0) {
            continue;
        }
        FD_SET(children[i].pipe_fd, readfds);
        if (*maxfd < children[i].pipe_fd) {
            *maxfd = children[i].pipe_fd;
        }
    }
}

void make_child_worker(int idx, int listen_fd) {
    void do_job(int);

    int fds[2];
    //socketpairs创建一对unix域套接字，可以互相通信，且是全双工
    //而pipe创建的是单向的
    int ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);
    if (ret < 0) {
        printf("socketpair err: %d\n", errno);
        exit(1);
    }
    pid_t cid;
    if ((cid = fork()) > 0) {
        //parent process
        close(fds[1]);
        children[idx].pid = cid;
        children[idx].pipe_fd = fds[0];
        children[idx].status = 0;
        printf("create child: %d pid %d, pipefd %d\n", idx, cid, fds[0]);
        return;
    }

    //child process
    dup2(fds[1], STDERR_FILENO);
    close(listen_fd);
    close(fds[0]);
    close(fds[1]);
    do_job(fds[1]);
}

int main() {
    int listen_fd = tcp_safe_listen(kListenPort);
    children = calloc(nchild, sizeof(Child));
    if (children == NULL) {
        perror("calloc");
        exit(1);
    }

    for (int i = 0; i < nchild; ++i) {
        make_child_worker(i, listen_fd);
    }

    //子进程fork之后再设置信号处理函数
    void handle_int(int);
    signal(SIGINT, handle_int);

    fd_set readfds;
    int maxfd;
    for ( ; ; ) {
        build_fd_set(listen_fd, &readfds, &maxfd);
        int ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        printf("select with %d ready\n", ready);
        if (ready <= 0) {
            continue;
        }
        int count = 0;
        if (FD_ISSET(listen_fd, &readfds)) {
            handle_listen(listen_fd);
            ++count;
            if (count >= ready) {
                continue;
            }
        }
        for (int j = 0; j < nchild; ++j) {
            if (FD_ISSET(children[j].pipe_fd, &readfds)) {
                handle_child_msg(j);
                ++count;
                if (count >= ready) {
                    break;
                }
            }
        }
    }

    return 0;
}


void handle_int(int sig) {
    printf("handle_int\n");

    //杀死所有子进程
    for (int i = 0; i < sizeof(children)/sizeof(children[0]); ++i) {
        kill(children[i].pid, SIGTERM);
    }

    //wait for all children to terminate
    while (wait(NULL) > 0 ) {
        ;
    }
    output_statistic();

    exit(0);
}



void do_job() {
    printf("child %d do_job\n", getpid());

    for ( ; ; ) {
        //读取 master 传过来的连接
        int connfd = recv_fd(STDERR_FILENO);
        if (connfd <= 0) {
            printf("recv_fd error: %d\n", errno);
            exit(1);
        }

        //处理连接
        void handle_conn(int);
        handle_conn(connfd);

        //告诉 master，我又可以服务其他链接了
        int service = 1;
        int ret = write(STDERR_FILENO, &service, sizeof(service));
        printf("child write back: %d\n", ret);
    }
}

void handle_conn(int connfd) {
    printf("children %d handle client %d\n", getpid(), connfd);
    
    ssize_t n;
    static char buff[512];
    for( ; ; ) {
        printf("start reading connection %d\n", connfd);
        if ((n = read(connfd, buff, sizeof(buff))) == 0) {
            printf("children %d client %d closed by peer\n", getpid(), connfd);
            //此时connfd被客户端关闭，处于半连接状态，调用close，完成四次挥手的后两步
            close(connfd);
            //本连接处理完毕，退出 handle_conn，继续服务其他连接
            return;
        }
        if (n < 0) {
            printf("read error %d\n", errno);
            return;
        }
        printf("read %zd bytes from connection %d\n", n, connfd);
        write(connfd, buff, n);
    }
}
