#include"chatserver.hpp"
#include"chatservice.hpp"
#include"json.hpp"
using json = nlohmann::json;

ChatServer::ChatServer(EventLoop *loop,const InetAddress &listenAddr,const string &nameArg)
                :_server(loop,listenAddr,nameArg),_loop(loop){
                //注册回调函数
                _server.setConnectionCallback(bind(&ChatServer::onConnection,this,_1));
                //注册信息回调
                _server.setMessageCallback(bind(&ChatServer::onMessage,this,_1,_2,_3));
                //设置线程数量
                _server.setThreadNum(4);
}   

void ChatServer::start(){
    _server.start();
}
//上报连接信息
void ChatServer::onConnection(const TcpConnectionPtr &conn){
    if(conn->connected()){
        cout<<conn->peerAddress().toIpPort()<<" -> "<<conn->localAddress().toIpPort()<<" state:online"<<endl;
    }else{
       conn->shutdown();
        //TODO 用户退出，注销用户信息
    }
}
//上报读写事件
void ChatServer::onMessage(const TcpConnectionPtr &conn,Buffer *buffer,Timestamp time){
    string msg=buffer->retrieveAllAsString();
    json js=json::parse(msg);//反序列化
    //通过js["msgid"]获取消息内容 目的：完全解耦网络模块的代码和业务模块的代码
    auto msghander=ChatService::instance()->getHandler(js["msgid"].get<int>()); 
    //回调消息绑定好的事件处理器，来执行相应的业务处理
    msghander(conn,js,time);
}   