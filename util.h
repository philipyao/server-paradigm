#ifndef __UTIL_H__
#define __UTIL_H__

#include <sys/types.h>

int tcp_safe_listen(int port);
void output_statistic();
ssize_t write_fd(int fd, int sendfd);
int recv_fd(int sock);

#endif //__UTIL_H__