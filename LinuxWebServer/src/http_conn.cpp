#include "http_conn.h" 

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request"; 
const char* error_400_form = "400 error"; // 表示客户端请求的报文有错误，但只是个笼统的错误。
const char* error_403_title = "Forbidden";
const char* error_403_form = "403 error"; // 表示服务器禁止访问资源，并不是客户端的请求出错。
const char* error_404_title = "Not Found";
const char* error_404_form = "404 error"; // 表示请求的资源在服务器上不存在或未找到，所以无法提供给客户端。
const char* error_500_title = "Internal Error";
const char* error_500_form = "500 error"; // 表示服务器内部错误

// 静态成员变量外部初始化
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
const int http_conn::FILENAME_LEN = 200;
const int http_conn::READ_BUFFER_SIZE = 2048;
const int http_conn::WRITE_BUFFER_SIZE = 1024;

const char* doc_root = "/home/zhe/LinuxNetwork_Learning/LinuxWebServer/resources";

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

// 关闭连接
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
    }
    return NO_REQUEST;       
}

// 写HTTP响应
bool http_conn::write()
{
    int temp;
    if (bytes_to_send == 0)
    {
        // 将要发送的字节为0，这一次响应结束
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        // 分散写,将数据从多个缓冲区 (iovec 结构体数组) 写入到文件描述符指定的文件或者套接字中
        // 如果成功，返回写入的字节数
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            // fixme 为什么这样处理？
            m_iv[0].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 没有数据要发送了
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

// 往写缓冲区写入待发送的数据
bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    // va_list 类型的变量 arg_list 来存储可变参数列表，
    // 并使用 va_start 宏初始化 arg_list，使其指向参数列表中的第一个可变参数 format。
    va_list arg_list;
    va_start(arg_list, format);
    // vsnprintf 函数将格式化的字符串写入 m_write_buf 中，从 m_write_idx 索引位置开始写入。最多写 WRITE_BUFFER_SIZE - 1 - m_write_idx
    // vsnprintf 的返回值 len 是实际写入缓冲区的字符数（不包括结尾的 null 字符）
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= WRITE_BUFFER_SIZE - 1 - m_write_idx)
    {
        return false;
    }
    m_write_idx += len; // 更新写缓冲区末尾后面的位置，以便下一次写入操作
    va_end(arg_list); // va_end 宏结束对可变参数列表的处理，确保资源释放和内存安全性。
    return true;
}

// 添加响应状态行
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加响应头
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

// 添加响应内容长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

// 添加响应是否为长连接行
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger == true ? "keep-alive" : "close");
}

// 添加响应空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加响应体内容
bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

// 添加响应体内容类型格式
bool http_conn::add_content_type()
{
    return add_response("Content-Type: %s\r\n", "text/html");
}

// 根据HTTP服务器请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
            {
                return false;
            }
            break;
        }

        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
            {
                return false;
            }
            break;
        }

        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
            {
                return false;
            }
            break;
        }

        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
            {
                return false;
            }
            break;
        }

        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        
        default:
        {
            return false;
        }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
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
    if (!m_url) { return BAD_REQUEST; }

    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';

    char* method = text;
    if (strcasecmp(method, "GET") == 0) // strcasecmp:忽略大小写比较
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
    *m_version++ = '\0'; // m_url = /index.html
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // http://192.168.1.1:10000/index.html
    // 比较m_url 字符串的前 7 个字符与 "http://" 是否完全相同，相同返回0，不同返回非零值
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7; // 192.168.1.1:10000/index.html
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
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
    strcpy(m_real_file, doc_root); // 复制字符串
    int len = strlen(doc_root);
    // FILENAME_LEN - len - 1：指定要复制的最大字符数。-1是为了留出存放'\0'
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获取m_real_file文件的相关状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    
    // 判断访问权限
    // S_IROTH：这个宏定义了其他用户的读取权限位
    // 按位与为非零值，说明没有读取权限
    if (!(m_file_stat.st_mode) & S_IROTH)
    {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否为目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY); 
    /**
     * @brief 创建内存映射，将文件映射到内存中的地址空间，允许程序员直接在用户空间访问文件内容，而无需通过标准的文件 I/O 接口来读取文件内容。
     *        这种内存映射的方式通常可以提高文件读取的效率，并且在某些场景下更方便地处理大文件或需要频繁访问的文件。
     *    0：指定内核自动选择映射地址。
     *    m_file_stat.st_size：指定映射区域的大小，即文件的大小。
     *    PROT_READ：指定映射区域的保护方式，这里是只读的，即允许读取映射区域的内容。
     *    MAP_PRIVATE：指定映射的方式为私有映射，对映射的修改不会影响原文件。
     *    fd：是一个已打开文件的文件描述符，用于标识要映射的文件。
     *    0：表示映射的起始位置在文件中的偏移量，这里从文件的起始位置开始映射。
     */
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap()
{
    if (m_file_address)
    {
        // 取消内存映射，释放资源
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
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
        // 防止同一个通信被不同的线程处理
        event.events | EPOLLONESHOT;
    }
 
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中移除监听的文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
