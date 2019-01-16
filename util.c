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