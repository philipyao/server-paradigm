#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <pthread/pthread.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "util.h"


static int listen_port = 9527;
static pid_t *children = NULL;
static const int nchild = 10;

//互斥锁需要多个进程共享，因此要用mmap来映射成shared的，不能直接分配成全局变量
//未初始化的全局变量是存放在各进程的bss段，如果fork子进程，会拷贝的，不会共享
static pthread_mutex_t *pmutex;
static pthread_mutexattr_t attrmutex;

int main() {
    int listen_fd = tcp_safe_listen(listen_port);

    /* Initialise attribute to mutex. */
    pthread_mutexattr_init(&attrmutex);
    pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);

    /* Initialise mutex. */
    void *ptr = mmap(NULL, sizeof(pthread_mutex_t), 
                    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, 
                    -1, 0);
    if (ptr == MAP_FAILED) {
        printf("mmap error\n");
        exit(1);
    }
    pmutex = (pthread_mutex_t *)ptr;
    pthread_mutex_init(pmutex, &attrmutex);

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

    pthread_mutex_destroy(pmutex);
    pthread_mutexattr_destroy(&attrmutex);
    munmap(pmutex, sizeof(*pmutex));

    exit(0);
}



void do_job(int listen_fd) {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    void handle_conn(int, struct sockaddr_in*);

    for ( ; ; ) {
        // mutex锁保证同时只有一个进程能 accept
        pthread_mutex_lock(pmutex);
        printf("children %d is accepting\n", getpid());
        int connfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &clilen);
        pthread_mutex_unlock(pmutex);
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
