#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<exception>
#include<sys/epoll.h>
#include<errno.h>
#include<fcntl.h>
#include<signal.h>
#include<semaphore.h>
#include "locker.h"
#include "threadpool.h"
#include"http_conn.h"

#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000

void addsig(int sig, void(handler)(int)){
    struct  sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//添加文件描述符到epoll
extern void addfd(int epollfd, int fd, bool one_shot);
//将epoll中的文件描述符删除
extern void removefd(int epollfd, int fd);
//修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[]){
    if(argc <= 1){
        printf("按照格式输入: %s port_number",basename(argv[0]));
        exit(-1); 
    }

    int port = atoi(argv[1]);

    addsig(SIGPIPE, SIG_IGN);

    //创建线程池
    threadpool<http_conn> *pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...){
         printf("1");
        exit(-1);
    }
   

    //保存客户信息
    http_conn* users = new http_conn[MAX_FD];

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1){
        perror("SOCKET");
        exit(-1);
    }
    //端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    int bfd = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(bfd == -1){
        perror("bind");
        exit(-1);
    }


    //监听
    listen(listenfd, 5);

    //epoll
    epoll_event event[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    //添加监听套接字到epoll
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;
    
    while(1){
        int num = epoll_wait(epollfd, event, MAX_EVENT_NUMBER, -1);
        if((num <0) && errno != EINTR)//默认是阻塞的，当有数据来的时候不阻塞也会返回-1
        {
            printf("epoll failure!\n");
            break;
        }

        for(int i=0; i<num; i++){
            int sockfd = event[i].data.fd;  
            if(sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                
                int connfd = accept(listenfd, (struct sockaddr*)&client_address,&client_addrlen);
                if(connfd == -1){
                    perror("accept");
                    exit(-1);
                }

                if(http_conn::m_user_count >= MAX_FD){
                    //满了
                    close(connfd);
                    continue; 
                }

                users[connfd].init(connfd, client_address);
            }
            else if(event[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            }else if(event[i].events & EPOLLIN) {
                if(users[sockfd].read()){
                    pool->append(users + sockfd);
                    
                }else{
                    users[sockfd].close_conn();
                }
            }else if(event[i].events & EPOLLOUT){
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }
            }
        }


    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;


    return 0;
}