#ifndef CHATSERVER_HPP
#define CHATSERVER_HPP
#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<iostream>
using namespace muduo;
using namespace muduo::net;
using namespace std;

class ChatServer{
private:
    TcpServer _server;  //服务器对象
    EventLoop *_loop;   //指向事件循环对象
    void onConnection(const TcpConnectionPtr &);//连接回调函数
    void onMessage(const TcpConnectionPtr &,Buffer *,Timestamp);//消息回调函数 上报读写事件

public://初始化聊天服务器对象
    ChatServer(EventLoop *loop,const InetAddress &listenAddr,const string &nameArg);
    void start();//启动服务
};
#endif