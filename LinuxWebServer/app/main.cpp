#include <iostream>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h> // 定义了Internet地址族结构体，如 struct sockaddr_in，以及一些与网络通信相关的常量和函数原型。
#include <arpa/inet.h>  // inet_addr、inet_ntoa、inet_pton 和 inet_ntop，用于在二进制形式与文本形式之间进行IP地址的转换。
#include <unistd.h>     // 包含了诸如系统调用（如 fork、exec、pipe）和对文件描述符的操作（如 close、read、write）等函数原型。
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

// 添加信号捕捉
void adddsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

int main(int argc, char const *argv[])
{
    if (argc <= 1)
    {
        exit(-1);
    }

    // 获取端口号
    int port = atoi(argv[1]);

    return 0;
}
