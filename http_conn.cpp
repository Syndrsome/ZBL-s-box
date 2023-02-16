#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


//网站根目录
const char* doc_root = "/home/zhangbolin/linux/webser/resources";

extern int setnonblocking(int fd){
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

//添加文件描述符到epoll
extern void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; //用户异常断开会触发

    if(one_shot){
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}
//将epoll中的文件描述符删除
extern void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符,并且重置EPOLLONESHOT事件
extern void modfd(int epollfd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLET  | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//初始化sock的信息
void http_conn::init(int sockfd, const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;
    int reuse = 1;
     setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

     addfd(m_epollfd, sockfd, true);
     m_user_count++;
     init();
}

//初始化其余的信息
void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_index = 0;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = false;
    m_content_length = 0;

    m_write_index = 0;

    bytes_to_send = 0;
    bytes_have_send = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
}

//关闭链接
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_conn::read(){
    if(m_read_index >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read=0;
    while(true){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                //没有数据会传输这两个信号
                break;
            }
            return false;
            }else if(bytes_read == 0){
                return false;
            }
            m_read_index += bytes_read;
        }
        printf("读到了数据：%s\n",m_read_buf);
        return true;
    }   
    


//主状态机
http_conn::HTTP_CODE http_conn::process_read()//解析HTTP请求
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text =0;

    while((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)  //解析到一行完整数据，或者解析到了请求体，也就是完成数据
    ||(line_status = parse_line()) == LINE_OK){
        //得到一行数据
        text = getline();
        m_start_line = m_checked_index;
        printf("get 1 http line:%s\n",text);

        
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    return do_request();
                }

            }
            case CHECK_STATE_CONTENT:{
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;

            }
            default:
            return INTERNAL_ERROR;
        }

    }
    return NO_REQUEST;
}
//得到一个完整的申请，处理器中的属性

http_conn::HTTP_CODE http_conn::do_request(){
    //doc_root : /home/zhangbolin/linux/webser
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file+len, m_url, FILENAME_LEN-len-1);
    if(stat(m_real_file, &m_file_stat) <0){
        return NO_RESOURCE;
    }
    //判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }
    //判断目录
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    //创建内存映射
    m_file_adress = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

//释放
void http_conn::unmap(){
    if(m_file_adress){
        munmap(m_file_adress,m_file_stat.st_size);
        m_file_adress = 0;
    }
}


//请求首行分析
http_conn::HTTP_CODE http_conn::parse_request_line(char * text){
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';
    char *method = text;
    if(strcasecmp(method, "GET") == 0){
        m_method = GET;
    }else {
        return BAD_REQUEST;
    }
    //index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    if(strcasecmp(m_version,"HTTP/1.1") != 0){
        return BAD_REQUEST;
    }

    if(strncasecmp(m_url,"http://", 7) == 0){
        m_url +=7;
        m_url = strchr(m_url, '/');
        if(!m_url || m_url[0] != '/'){
            return BAD_REQUEST;
        }
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(strncasecmp(text, "connection:", 11) ==0){
        text += 11;
        text += strspn(text,"\t");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }else if(strncasecmp(text,"Content-Length:", 15) == 0){
        text +=15;
        text += strspn(text, "\t");
        m_content_length = atol(text);
    }else if(strncasecmp(text,"Host:", 5) == 0){
        text+=5;
        text += strspn(text,"\t");
        m_host = text;
    }else{
        printf("unknow header %s\n", text);
    }
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_content_length + m_checked_index <= m_read_index){
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_checked_index < m_read_index; ++m_checked_index){
        temp = m_read_buf[m_checked_index];
        if(temp == '\r'){
            if((m_checked_index +1) == m_read_index){
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_index + 1] == '\n'){
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n'){
            if((m_checked_index>1) && (m_read_buf[m_checked_index - 1] =='\r')){
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    
    
    return LINE_OPEN;
}


void http_conn::process(){
    //解析HTTP
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    //生成相应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
    printf("解析HTTP\n");
}


bool http_conn::write(){
    int temp = 0;
    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1){
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <=1){
            if(errno == EAGAIN){
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
            m_iv[1].iov_base = m_file_adress + (bytes_have_send - m_write_index);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if(bytes_to_send <=0){
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if(m_linger){
                init();
                return true;
            }else{
                return false;
            }
        }
    }
}


bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(! add_content(error_500_form)){
                return false;
            }
            break;

        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(! add_content(error_400_form)){
                return false;
            }
            break;

        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(! add_content(error_404_form)){
                return false;
            }
            break;

        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(! add_content(error_403_form)){
                return false;
            }
            break;

        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_index;
            m_iv[1].iov_base = m_file_adress;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_index + m_file_stat.st_size;
            return true;
        
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_index;
    m_iv_count = 1;
    bytes_to_send = m_write_index;
    return true;
}

bool http_conn::add_headers(int content_length){
    add_content_length(content_length);
    add_content_type();
    add_linger();
    add_blank_line();
}
bool  http_conn::add_content_length(int content_length){
    return add_response("Content-Length: %d\r\n", content_length);
}
bool  http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n","text/html");
}
bool  http_conn::add_linger(){
    return add_response("Connection: %s\r\n", m_linger == true ? "keep-alive" : "close");
}
bool  http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}
bool http_conn::add_content(const char* content){
    return add_response("%s", content);
}

bool http_conn::add_response(const char* format, ...){
    if(m_write_index >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_index, WRITE_BUFFER_SIZE-1-m_write_index, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE -1-m_write_index)){
        return false;
    }
    m_write_index+=len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n","HTTP/1.1", status, title);
}