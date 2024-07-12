#include "http_conn.h" 

// 静态成员变量外部初始化
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
const int http_conn::READ_BUFFER_SIZE = 2048;
const int http_conn::WRITE_BUFFER_SIZE = 1024;


// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象中
    addfd(m_epollfd, m_sockfd, true);
    m_user_count++;
}

// 关闭连接（不是关闭文件描述符）
void http_conn::close_conn()
{
    if (m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    // 读取到的字节
    int bytes_read = 0;
    while (true)
    {
        /**
         * @brief recv 函数
         * recv 函数是一个系统调用，用于从指定的套接字（m_sockfd）接收数据,返回接收到的字节数，或者在发生错误时返回一个错误代码。
         * m_sockfd：套接字描述符，指示从哪个套接字接收数据。
         * m_read_buf + m_read_idx：接收数据存放的位置。这里使用了指针算术，
         * m_read_buf 是一个指向字符或字节数据的数组指针，m_read_idx 是一个索引，表示从 m_read_buf 的第 m_read_idx 个位置开始存放接收到的数据。
         * READ_BUFFER_SIZE - m_read_idx：接收数据的最大字节数。
         * 0：一般用于指定接收数据时的额外选项，这里是默认设置，表示没有特殊选项。
         */
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) // 没有数据
            {
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            // 对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    std::cout << "读取到了数据: " << m_read_buf << std::endl;
    return true;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    
}


bool http_conn::write()
{
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process()
{
    // 解析HTTP请求

    // 生成响应
}

// 设置文件描述符非阻塞
void setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

// 向epoll中添加需要监听的文件描述符
void addfd(int epollfd, int fd, bool one_shot)
{
    // 创建一个epoll事件
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
    {
        event.events | EPOLLONESHOT;
    }
 
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    
}

// 从epoll中移除监听的文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
