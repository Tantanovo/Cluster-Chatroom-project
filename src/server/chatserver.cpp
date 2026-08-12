#include"chatserver.hpp"
#include"chatservice.hpp"
#include"json.hpp"
#include<muduo/base/Logging.h>
#include<arpa/inet.h>
#include<cstring>
using json=nlohmann::json;

ChatServer::ChatServer(EventLoop *loop,const InetAddress &listenAddr,const string &nameArg)
                :_server(loop,listenAddr,nameArg),_loop(loop){
                //注册回调函数
                _server.setConnectionCallback(bind(&ChatServer::onConnection,this,_1));
                //注册信息回调
                _server.setMessageCallback(bind(&ChatServer::onMessage,this,_1,_2,_3));
                //设置线程数量
                _server.setThreadNum(4);
}   
//启动服务
void ChatServer::start(){
    _server.start();
}

//统一发送出口：先写4字节网络序长度，再写body
void ChatServer::sendWithHeader(const TcpConnectionPtr &conn,const string &body){
    if(conn==nullptr||!conn->connected())return;
    uint32_t len=htonl(static_cast<uint32_t>(body.size()));
    string frame;
    frame.reserve(kHeaderLen+body.size());
    frame.append(reinterpret_cast<const char*>(&len),kHeaderLen);
    frame.append(body);
    conn->send(frame);
}

//上报连接信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn){
    if(conn->connected()){
        LOG_INFO<<conn->peerAddress().toIpPort()<<" -> "
                <<conn->localAddress().toIpPort()<<" state:online";
    }else{
        //连接断开：清理在线状态、取消redis订阅
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

//上报读写事件
void ChatServer::onMessage(const TcpConnectionPtr &conn,Buffer *buffer,Timestamp time){
    //关键：用while循环处理，一次可读事件里可能到达 0.5帧、1帧或N帧
    while(buffer->readableBytes()>=kHeaderLen){
        //1.先"偷看"长度头，不消费缓冲区
        uint32_t beLen=0;
        ::memcpy(&beLen,buffer->peek(),kHeaderLen);
        const uint32_t bodyLen=ntohl(beLen);

        //2.长度非法直接断连，防止恶意包
        if(bodyLen==0||bodyLen>kMaxFrameLen){
            LOG_ERROR<<"illegal frame length "<<bodyLen
                     <<" from "<<conn->peerAddress().toIpPort()<<", closing";
            conn->shutdown();
            return;
        }

        //3.半包：body还没收全，保留数据等下一次可读事件
        if(buffer->readableBytes()<kHeaderLen+bodyLen){
            break;
        }

        //4.完整一帧：取出header和body
        buffer->retrieve(kHeaderLen);
        string body=buffer->retrieveAsString(bodyLen);

        //5.异常隔离：一条畸形报文只影响这一个连接，绝不让异常逃出回调
        //  （改造前 json::parse 抛出的异常无人捕获，会终止整个进程）
        try{
            json js=json::parse(body);
            if(!js.contains("msgid")||!js["msgid"].is_number_integer()){
                LOG_ERROR<<"frame without valid msgid, ignored";
                continue;
            }
            auto handler=ChatService::instance()->getHandler(js["msgid"].get<int>());
            handler(conn,js,time);
        }catch(const json::exception &e){
            LOG_ERROR<<"json parse failed: "<<e.what()
                     <<" raw="<<body.substr(0,128);
            conn->shutdown();
            return;
        }catch(const std::exception &e){
            LOG_ERROR<<"handler threw: "<<e.what();
            continue;//业务异常不牵连连接
        }
    }
}
