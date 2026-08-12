#include"chatserver.hpp"
#include"chatservice.hpp"
#include<muduo/base/Logging.h>
#include<iostream>
#include<signal.h>
#include<cstdio>
#include<cstdlib>
using namespace std;

static EventLoop *g_loop=nullptr;

//优雅退出：把online用户重置为offline，再退出事件循环
//（改造前是 signal(SIGPIPE, resetHandler)，SIGPIPE被覆盖成了exit(0)，
//  任何客户端异常断开都会让整个服务器退出 —— 这是muduo默认SIG_IGN被覆盖导致的bug）
void gracefulExit(int){
    ChatService::instance()->reset();
    if(g_loop!=nullptr){
        g_loop->quit();
    }
}

int main(int argc,char **argv){
    //【重要修正】SIGPIPE恢复为SIG_IGN。
    //  muduo在EventLoop构造时已把SIGPIPE设为SIG_IGN，原来的代码又
    //  signal(SIGPIPE, resetHandler) 把它覆盖成一个内部调用 exit(0) 的函数，
    //  导致任意客户端异常断开(向已关闭连接写数据)时，整个服务器进程退出。
    signal(SIGPIPE,SIG_IGN);
    //优雅退出挂在SIGINT/SIGTERM上，用loop.quit()而不是exit()
    signal(SIGINT,gracefulExit);
    signal(SIGTERM,gracefulExit);

    //【重要修正】端口不再硬编码，支持同一台机器起多个节点做集群
    if(argc<3){
        fprintf(stderr,"usage: %s <ip> <port>\n",argv[0]);
        fprintf(stderr,"  e.g. %s 127.0.0.1 6000\n",argv[0]);
        return 1;
    }
    const char *ip=argv[1];
    uint16_t port=static_cast<uint16_t>(atoi(argv[2]));

    EventLoop loop;//创建事件循环对象
    g_loop=&loop;
    InetAddress addr(ip,port);//监听地址由命令行指定
    ChatServer server(&loop,addr,"ChatServer");//创建服务器对象
    LOG_INFO<<"ChatServer starting at "<<ip<<":"<<port;
    server.start();//启动服务器
    loop.loop();//启动事件循环
    LOG_INFO<<"ChatServer exited gracefully";
    return 0;
}
