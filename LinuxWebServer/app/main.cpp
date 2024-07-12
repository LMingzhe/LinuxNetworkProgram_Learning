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

#define MAX_FD 65535 // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

extern void addfd(int epollfd, int fd, bool ont_shot);
extern void removefd(int epollfd, int fd);


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

    // 对 SIGPIE 信号进行处理
    adddsig(SIGPIPE, SIG_IGN); // fixme 这里不理解，后面补充一下

    // 创建线程池，初始化线程池
    threadpool<http_conn>* pool = nullptr;
    try
    {
        pool = new threadpool<http_conn>; 
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        exit(-1);
    }
    
    // 创建一个数组用于保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];
    
    // PF_INET 指定协议族，表示 IPv4 协议族, 字节流 ，0 默认TCP
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);  
    if (listenfd == -1)
    {
        std::cout << "socket 创建失败" << std::endl;
        exit(-1);
    }

    /**
     * @brief 设置端口复用
     * SO_REUSEADDR 是一个套接字选项常量，用于告诉内核在 bind 调用中重新使用处于 TIME_WAIT 状态的套接字地址。
     * 这对于服务器程序在关闭后快速重启并绑定到同一端口号是非常有用的。
     */
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 任何IP地址都可连接
    address.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr*)&address, sizeof(address)) == 0)
    {
        std::cout << "Bind Successful." << std::endl;
    } else {
        std::cout << "Bind Fail." << std::endl;
        exit(-1);
    }

    // 监听
    listen(listenfd, 5);

    // 创建epoll对象，事件数组
    epoll_event events[MAX_EVENT_NUMBER];
    // 5:个参数指定 epoll 实例中能同时监视的文件描述符的数目的一个建议值。Linux 内核会根据这个值为 epoll 实例分配合适的内存空间。这个参数并不是一个严格限制，只是一个提示值。
    int epollfd = epoll_create(5);
    http_conn::m_epollfd = epollfd;

    // 将监听的文件描述符添加到eppoll对象中
    addfd(epollfd, listenfd, false);
    // http_conn::m_epollfd = epollfd;

    while (true)
    {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (num < 0 && errno != EINTR) // EINTR 系统中断
        {
            std::cout << "epoll failure" << std::endl;
        }

        // 遍历事件数组
        for (int i = 0; i < num; i++)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) // 如果是一个连接事件
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                // accept函数返回一个新的套接字描述符，用于与刚刚建立连接的客户端通信。如果出现错误，返回 -1，并设置全局变量 errno 表示错误类型。
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);
                if (connfd == -1)
                {
                    std::cout << "connect failure" << std::endl;
                }

                if (http_conn::m_user_count >= MAX_FD)
                {
                    // 目前连接数满了
                    // 给客户端写一个信息：服务器内部正忙
                    close(connfd);
                    continue;
                }

                // 将新的客户数据初始化，放到数组中
                users[connfd].init(connfd, client_address);

            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                /**
                 * @brief 
                 * EPOLLRDHUP 表示对端断开连接或者半关闭状态，即远程端关闭了连接或者发生了对端异常的情况。
                 * EPOLLHUP 表示发生了挂起事件，通常表示连接被挂起，或者对端关闭了连接，或者出现了其他异常情况。
                 * EPOLLERR 表示发生了错误事件，通常是指出现了一些异常情况，如连接错误或其他错误。
                 * 在这段代码中，& 是位操作符，用来检查 events[i].events 中是否包含 EPOLLRDHUP、EPOLLHUP 或 EPOLLERR 中的任何一个标志。
                 * 如果 events[i].events 中包含这些标志中的任意一个，表达式的结果就不为零，表示发生了对应的事件。
                 */

                // 对方异常断开或者错误等事件
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) { // 如果是一个写事件
                if (users[sockfd].read())
                {   
                    // 一次性把所有数据都读完
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events * EPOLLOUT) {
                if (!users[sockfd].write())
                {
                    // 一次性写完所有数据
                    users[sockfd].close_conn();
                }
            }
        }
    }
    
    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;
    


    return 0;
}
