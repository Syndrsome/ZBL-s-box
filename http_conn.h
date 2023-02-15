#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include<sys/epoll.h>
#include<stdio.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<signal.h>
#include<sys/mman.h>
#include<errno.h>
#include<sys/uio.h>
#include "locker.h"
#include<string.h>
#include<stdarg.h>
#include<sys/uio.h>
#include<sys/mman.h>


class http_conn{
    public:
    static int m_epollfd;
    static int m_user_count; //用户数量
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    static const int FILENAME_LEN = 200;

     // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    http_conn(){

    }

    ~http_conn(){

    }

    void process();
    void init(int sockfd, const sockaddr_in &addr);
    void close_conn();
    bool read();//非阻塞的读
    bool write();//非阻塞的写

    char* getline(){return m_read_buf + m_start_line;}

    HTTP_CODE process_read();//解析HTTP请求
    HTTP_CODE parse_request_line(char * text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();

    LINE_STATUS parse_line();

    void unmap(); //释放内存映射

    bool process_write(HTTP_CODE ret);
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);


    private:
    int m_sockfd; //这个fd的socket
    sockaddr_in m_address;


    char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
    int m_read_index; //标记读入的客户端数据的下一个位置

    int m_checked_index; //分析在都缓冲区字符的位置
    int m_start_line; //当前解析行的位置

    CHECK_STATE m_check_state; //主状态机位置
    void init(); //初始化连接其余的信息

    char *m_url; //请求目标文件名
    char *m_version; //协议版本
    METHOD m_method; //请求方法
    char* m_host; //主机
    int m_content_length;
    bool m_linger; //是否要保持连接

    char m_real_file [FILENAME_LEN]; //目标文件目录
    struct stat m_file_stat;//文件状态
    char* m_file_adress;//客户请求的文件被mmap到内存中的起始位置

    int m_write_index; //写指针
    char m_write_buf[WRITE_BUFFER_SIZE];
    struct iovec m_iv[2];
    int m_iv_count;
    
    int bytes_to_send;
    int bytes_have_send;



};


#endif