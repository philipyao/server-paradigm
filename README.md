## UnixNetProgramming 第30章 服务器设计范式

### 并发服务器，每个连接fork一个子进程
对应文件 fork_on_conn.c；现场 fork 进程，效率很低，且子进程数量有限

### 子进程池，所有子进程 accept 同一 fd，无保护
对应文件 prefork_all_accept.c; 该实现需要特定系统支持。多进程同时 accept 的实现原理如下：

* 每个子进程复制 listen_fd 描述符，所有这些 listen_fd 都指向同一个 file 结构 （file指向最终的socket结构）
* file 结构维护一个引用计数，N个进程拥有 listen_fd 则引用计数是 N
* 所有进程都阻塞在 listen_fd 的 accept 系统调用上时，实际是在 socket结构的 so_timeo 成员上进入睡眠；当有新连接过来时，所有进程都被唤醒，但只有最先运行的进程获得连接(从已完成三次握手的队列里取，队列大小取决于 backlog 大小), 其他进程继续睡眠。
* 这个问题称为惊群(thundering herd)，有一个连接过来时，所有进程(或线程)都被唤醒，会导致性能受损（进程越多越明显）
* 注意 多进程或线程同时 accept 有些内核不支持; 如果多个进程同时使用 select 多路复用来取代直接 accept，一般会出错

### 子进程池，所有子进程 accept 同一 fd，用锁保护
对应文件 prefork_accept_lock.c; 逻辑大部分和无锁保护的 accept 相同，区别在于 accept 调用前获得互斥锁。
需要注意的是，改锁是进程间共享的锁，不能通过全局变量的方式来声明，否则 fork 调用之后父子进程的锁其实是被拷贝了，相互没有关系；
如果需要做到进程间锁互斥，锁本身需要共享。通过 mmap 系统调用可以实现锁内存的共享; 锁用 pthread 提供的进程锁即可。
```c
/* Initialise mutex. */
void *ptr = mmap(NULL, sizeof(pthread_mutex_t), 
                PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, 
                -1, 0);

/* Initialise attribute to mutex. */
pthread_mutexattr_init(&attrmutex);
pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);
```

### 子进程池，父进程统一 accept，然后将连接描述符传给各子进程处理
对应文件 master_accept.c; 主要的难点在于：
* 子进程的调度算法
  * 这里父进程维护子进程的信息，每个子进程有忙闲标识；
  * 父进程在分发客户端连接时，查询子进程的信息，找出闲进程，选中后将其标记为忙状态；
  * 子进程处理完一个连接后（一般是客户端断开），则向父进程通知修改自己的忙闲状态，以便继续服务其他连接
  * 子进程通知父进程，采用的是unix域套接字(socketpair)。这里没有采用管道pipe，是因为pipe是单向的，而父进程也需要往子进程传递消息；
  * 注意所有的忙闲修改都在父进程完成，因此不存在并发问题；

* 父进程向子进程传递描述符
  * 使用unix域套接字传递套接字（客户端连接connfd）
  * 注意不能直接直接往pipe_fd里写connfd，因为每个进程的fd只是文件描述表的索引，两个进程的文件描述表一般都不同（父子进程fork的之后相同，但如果父子进程又打开了描述符，则不同)。fd作为描述表的索引，在进程内才有意义，直接在进程间传递没有意义
  * 关于进程间传递描述符，实际是重新创建了文件描述项，具体可以查看APUE或者UNP的相关章节

对于这种服务器模型，除了父进程往子进程传递描述符让子进程来read，也有的服务器模型是在master父进程中read，然后将read到的数据dispatch到子进程进行处理，这样实际的IO完全都在master做，子进程只是处理逻辑

### 并发服务器，每个连接一个线程
对应文件 thread_on_conn.c; 现场创建线程，效率也不太高，受限于系统支持的线程数量

### 线程池，所有线程 accept 同一 fd，锁保护
对应文件 prethread_accept_lock.c; 这种方式目前是所有的范式中最快的

### 线程池，主线程统一 accept，然后派发给各线程处理
主线程和各工作线程间通过环形队列通信，并加锁保护队列。主线程 accept 连接后，将其放入环形队列，工作子线程从环形队列中取数据来处理，所有线程间的同步是通过互斥锁和条件变量来进行。


## 综上各种范式，可知
* 进程池或者线程池的方案优于现场创建进程或者线程的方案；可以做的优化是动态扩容或者缩容
* 各子进程或者线程自行调用 accept 通常比父进程或者主线程统一调用accept然后再传递的效率要高，且更简单
* 线程的方案通常比进程快

## 代码说明
各代码例子在 MacOS 10.13.3 上运行通过

编译：
```
gcc fork_on_conn.c util.c
```

运行服务器：
```
$ ./a.out
```

运行客户端：
```
$ telnet localhost 9527
```