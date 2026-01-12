#ifndef CHATSERVICE_HPP
#define CHATSERVICE_HPP
#include<muduo/net/TcpConnection.h>
#include<unordered_map>
#include<functional>
#include"json.hpp"
using json=nlohmann::json;
using namespace std;
using namespace muduo;
using namespace muduo::net;
//表示处理消息的事件回调方法类型
using msghandler=function<void(const TcpConnectionPtr &conn,json &js,Timestamp time)>;

//聊天服务器业务类
class ChatService{
private:
    ChatService();//单例模式
    
    unordered_map<int,msghandler> _msgHandlerMap;//存储消息id和对应的业务处理方法
public:
    static ChatService* instance();//获取单例对象的接口函数

    void login(const TcpConnectionPtr &conn,json &js,Timestamp time);
    void reg(const TcpConnectionPtr &conn,json &js,Timestamp time);
    msghandler getHandler(int msgid);


};

#endif