#include "http_conn.h" 

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "400 error";
const char* error_403_title = "Forbidden";
const char* error_403_form = "403 error";
const char* error_404_title = "Not Found";
const char* error_404_form = "404 error";
const char* error_500_title = "Internal Error";
const char* error_500_form = "500 error";

// 静态成员变量外部初始化
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
const int http_conn::FILENAME_LEN = 200;
const int http_conn::READ_BUFFER_SIZE = 2048;
const int http_conn::WRITE_BUFFER_SIZE = 1024;

const char* doc_root = "/home/zhe/LinuxNetwork_Learning/LinuxWebServer/resources";

bool http_conn::add_response(const char* format, ...)
{
    // todo
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{

}

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

    init();
}

// 初始化连接其余的信息
void http_conn::init()
{
    m_read_idx = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_start_line = 0;
    m_checked_index = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;
    m_content_length = 0;
    m_host = 0;
    m_write_idx = 0;   
    bytes_to_send = 0;
    bytes_have_send = 0;


    bzero(m_read_buf, READ_BUFFER_SIZE);
    // memset(m_read_buf, 0, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
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

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) 
                || (line_status = parse_line()) == LINE_OK) // 解析到了一行完整的数据，或者解析到了请求体，也是完整的数据
    {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_index;
        std::cout << "get 1 http line: " << text << std::endl;

        // 主状态机处理
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                } else if (ret = GET_REQUEST) {
                    return do_request();
                }
                break;
            }

            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                line_status = LINE_OPEN; // 失败，设置为LINE_OPEN;
                break;
            }

            default:
            {
                return INTERNAL_ERROR;
            }
        }

        return NO_REQUEST;
    }
                
}


bool http_conn::write()
{
    //todo
    return true;
}

// 根据HTTP服务器请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    // todo
    switch (ret)
    {

    }
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    } else {
        modfd(m_epollfd, m_sockfd, EPOLLOUT);
    }
    
}

// 解析HTTP请求行，获得请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    // GET /index.html HTTP/1.1
    // 查找字符串 text 中第一个包含 " \t" 中任何字符的位置，并返回该位置的指针
    m_url = strpbrk(text, " \t");

    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';

    char* method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    // /index.html\0HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // http://192.168.1.1:10000/index.html
    // 比较m_url 字符串的前 7 个字符与 "http://" 是否完全相同，相同返回0，不同返回非零值
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7; // 192.168.1.1:10000/index.html
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER; // 主状态机变为检查请求头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0')
    {
        // 如果HTTP还有请求体信息，则还需要读取m_content_length字节的消息体
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理Connection头部字段，Connection：Keep-alive
        text += 11;
        // strspn返回字符串 text 开头连续包含在字符串 " \t" 中的字符数目（空格和制表符的集合）。
        text += strspn(text, " \t"); // text = strpbrk(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        std::cout << "unknow header: " << text << std::endl;
    }
    return NO_REQUEST;
}

// 解析请求体，这里没有真正的解析HTTP请求体，只是判断它是否被完整的读入
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if (m_read_idx >= m_content_length + m_checked_index)
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析一行数据，判断依据 '\r\n'
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for ( ; m_checked_index < m_read_idx; ++m_checked_index)
    {
        temp = m_read_buf[m_checked_index];
        if (temp == '\r')
        {
            if (m_checked_index + 1 == m_read_idx)
            {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_index + 1] == '\n') {
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_index > 1 && m_read_buf[m_checked_index - 1] == '\r')
            {
                m_read_buf[m_checked_index - 1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        return LINE_OPEN;
    }
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射
// 到内存地质m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // todo
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
