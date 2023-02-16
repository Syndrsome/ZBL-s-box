#ifndef LSTTIMER_H
#define LSTTIMER_H

#include<stdio.h>
#include<time.h>
#include<arpa/inet.h>

class util_timer;


struct client_data{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer{
public:
    util_timer():pre(NULL),next(NULL){}

public:
    time_t expire;//绝对时间
    client_data * user_data;
    util_timer *pre;
    util_timer *next;

    void (*cb_func)(client_data*);
};

class sort_timer_list{
public:
    sort_timer_list():head(NULL), tail(NULL){}
    ~sort_timer_list(){
        util_timer *tmp = head;
        while(tmp){
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    void add_timer(util_timer * timer){
        if(!timer){return ;}

        if(!head){
            head = tail = timer;
        }

        if(timer->expire < head->expire){
            timer->next = head;
            head->pre = timer;
            head = timer;
            return ;
        }
        add_timer(timer,head);
    }

    void adjust_timer(util_timer* timer){
        if(!timer) {
            return ;
        }

        util_timer* tmp = timer->next;
        if(!tmp || timer->expire < tmp->expire){
            return ;
        }

        if(timer == head){
            head = head->next;
            head->pre = NULL;
            timer->next = NULL;
            add_timer(timer);
        }else{
            timer->pre->next = timer->next;
            timer->next->pre = timer->pre;
            add_timer(timer);
        }
    }

    void del_timer(util_timer* timer){
        if(!timer) {return ;}
        if(head == timer  && timer == tail){
            delete timer;
            head = NULL;
            tail = NULL;
        }
        if(timer == head){
            head = head->next;
            head->pre = NULL;
            delete timer;
            return ;
        }
        if(timer == tail){
            tail = tail->pre;
            tail->next = NULL;
            delete timer;
            return ;
        }
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        delete timer;
        return ;
    }

    void tick(){
        if(!head ) {
            return ;
        }

        printf("time tick\n");
        time_t cur = time(NULL);
        util_timer* tmp = head;
        while(tmp){
            if(cur < tmp->expire){
                break;
            }

            tmp->cb_func(tmp->user_data);
            head = tmp->next;
            if(head){
                head->pre = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer *timer, util_timer *lst_head){
        util_timer* prev = lst_head;
        util_timer* tmp = lst_head->next;
        while(tmp){
            if(timer->expire < tmp->expire){
                timer->next = tmp;
                timer->pre = prev;
                prev->next = timer;
                tmp->pre = tmp;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if(!tmp){
            prev->next = timer;
            timer->pre = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

    util_timer *head;
    util_timer *tail;
};



#endif