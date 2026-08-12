#ifndef CHATSERVER_HPP
#define CHATSERVER_HPP

#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
#include<string>
using namespace std;
using namespace muduo;
using namespace muduo::net;

//聊天服务器网络模块
class ChatServer
{
public:
    ChatServer(EventLoop *loop,const InetAddress &listenAddr,const string &nameArg);
    void start();

    //统一的消息发送出口：给所有业务层调用
    //自动封装 [4字节网络序长度头 + JSON body]，保证收发协议对称
    static void sendWithHeader(const TcpConnectionPtr &conn,const string &body);

private:
    void onConnection(const TcpConnectionPtr &conn);
    void onMessage(const TcpConnectionPtr &conn,Buffer *buffer,Timestamp time);

    TcpServer _server;//muduo库的服务器功能对象
    EventLoop *_loop;//事件循环对象

    static const size_t kHeaderLen = 4;               //长度头字节数
    static const uint32_t kMaxFrameLen = 64 * 1024;   //单帧body上限，防恶意超大帧拖垮内存
};

#endif
