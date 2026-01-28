#ifndef CHATSERVICE_HPP
#define CHATSERVICE_HPP
#include<muduo/net/TcpConnection.h>
#include<unordered_map>
#include<functional>
#include<mutex>
#include"json.hpp"
using json=nlohmann::json;
using namespace std;
using namespace muduo;
using namespace muduo::net;
#include"usermodel.hpp"
//表示处理消息的事件回调方法类型
using msghandler=function<void(const TcpConnectionPtr &conn,json &js,Timestamp time)>;

//聊天服务器业务类
class ChatService{
private:
    ChatService();//单例模式
    
    //存储消息id和对应的业务处理方法
    unordered_map<int,msghandler> _msgHandlerMap;
    
    //存储在线用户的通信连接
    unordered_map<int,TcpConnectionPtr> _userConnMap;

    //定义互斥锁 保证_userConnMap的线程安全
    mutex _connMutex;

    UserModel _userModel;//数据操作类对象

public:
    static ChatService* instance();//获取单例对象的接口函数

    void login(const TcpConnectionPtr &conn,json &js,Timestamp time);//登录业务
    void reg(const TcpConnectionPtr &conn,json &js,Timestamp time);//注册业务
    msghandler getHandler(int msgid);//获取消息对应的处理器
    void clientCloseException(const TcpConnectionPtr &conn);//客户端异常退出


};

#endif