#include "util.h"
#include <sys/types.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#include <sys/time.h>
// for iov
#include <sys/uio.h>

#define SIZE_BACKLOG    10

int tcp_safe_listen(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        printf("create listen fd error: %d\n", errno);
        exit(1);
    }
    int reuse = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        printf("reuse listen addr error: %d\n", errno);
        exit(1);
    }

    struct sockaddr_in server, cli_addr;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
	server.sin_port = htons(port);
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

    return listen_fd;
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
}

//通过 fd 发送 sendfd 描述符
ssize_t write_fd(int fd, int sendfd)
{
    struct msghdr   msg;
    struct iovec    iov[1];

    int cmsgsize = CMSG_LEN(sizeof(int));
    static struct cmsghdr* cmptr = NULL;
    if (cmptr == NULL) {
        cmptr = (struct cmsghdr*)malloc(cmsgsize);
        cmptr->cmsg_level = SOL_SOCKET;
        cmptr->cmsg_type = SCM_RIGHTS; // we are sending fd.
        cmptr->cmsg_len = cmsgsize;
    }

    char c = 0;
    iov[0].iov_base = &c;
    iov[0].iov_len = 1;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = cmptr;
    msg.msg_controllen = cmsgsize;
    *(int *)CMSG_DATA(cmptr) = sendfd;

    return(sendmsg(fd, &msg, 0));
}

int recv_fd(int sock)
{
    int cmsgsize = CMSG_LEN(sizeof(int));
    static struct cmsghdr* cmptr = NULL;
    if (cmptr == NULL) {
        cmptr = (struct cmsghdr *)malloc(cmsgsize);
    }
    
    char c; // the max buf in msg.
    struct iovec iov[1];
    iov[0].iov_base = &c;
    iov[0].iov_len = 1;
    
    struct msghdr msg;
    msg.msg_iov = iov;
    msg.msg_iovlen  = 1;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = cmptr;
    msg.msg_controllen = cmsgsize;
    
    int ret = recvmsg(sock, &msg, 0);
    if (ret == -1) {
        perror("[recv_fd] recvmsg error");
        exit(1);
    }
    
    int fd = *(int *)CMSG_DATA(cmptr);
    return fd;
}