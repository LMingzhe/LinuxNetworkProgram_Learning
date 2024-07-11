#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include "locker.h"

class http_conn
{
public:
    static int m_epollfd; // 所有的socket上的事件都被注册到同一个epoll对象上
    static int m_user_count; // 统计用户的数量

    http_conn();

    void process();
    // 初始化新接收的连接
    void init(int sockfd, const sockaddr_in& addr); 

    ~http_conn();

private:

};



#endif