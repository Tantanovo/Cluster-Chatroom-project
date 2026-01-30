#include"chatservice.hpp"
#include<string>
#include<muduo/base/Logging.h>
#include<vector>
using namespace std;
using namespace muduo;
#include"public.hpp"
ChatService* ChatService::instance(){
    static ChatService service;
    return &service;
}
ChatService::ChatService(){
    //注册消息以及对应的Handler
    _msgHandlerMap.insert({LOGIN_MSG,bind(&ChatService::login,this,_1,_2,_3)});
    _msgHandlerMap.insert({REG_MSG,bind(&ChatService::reg,this,_1,_2,_3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG,bind(&ChatService::onechat,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG,bind(&ChatService::addfriend,this,_1,_2,_3)});
}

void ChatService::reset(){
    //把online状态的用户，设置成offline
    _userModel.resetState();
}

msghandler ChatService::getHandler(int msgid){
    //记录错误日志，msgid没有对应的处理函数
    auto it=_msgHandlerMap.find(msgid);
    if(it==_msgHandlerMap.end()){
        //返回一个默认的处理器 空操作
        return [=](const TcpConnectionPtr &conn,json &js,Timestamp time){
            LOG_ERROR<<"msgid:"<<msgid<<" can not find handler!";
        };
    }
    else return _msgHandlerMap[msgid];
}

// 处理登录业务
void ChatService::login(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int id=js["id"].get<int>();
    string password=js["password"];
    User user=_userModel.query(id);
    if(user.getId()==id&&user.getPassword()==password){
        if(user.getState()=="online"){
            //该用户已经登录，不能重复登录
            json response;
            response["msgid"]=LOGIN_ACK_MSG;
            response["errno"]=2;
            response["errmsg"]="该账号已经登录，不能重复登录";
            conn->send(response.dump());
            return;
        }
        else{
            //登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id,conn});
            }
            
            //登录成功 更新用户状态信息 state offline=>online
            user.setState("online");
            _userModel.updatestate(user);

            json response;
            response["msgid"]=LOGIN_ACK_MSG;
            response["errno"]=0;
            response["id"]=user.getId();
            response["name"]=user.getName();
            //查询是否有离线消息
            vector<string> vec=_offlineMsgModel.query(id);
            if(!vec.empty()){
                response["offlinemsg"]=vec;
                //读取该用户的离线消息后，删除掉
                _offlineMsgModel.remove(id);
            }
            //查询该用户的好友信息并返回
            vector<User> vecFriend=_friendModel.query(id);
            if(!vecFriend.empty()){
                vector<string> vec2;
                for(User &user:vecFriend){
                    json js;
                    js["id"]=user.getId();
                    js["name"]=user.getName();
                    js["state"]=user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"]=vec2;
            }

            conn->send(response.dump());
        }
    }
    else{
        //该用户不存在,登录失败
        json response;
        response["msgid"]=LOGIN_ACK_MSG;
        response["errno"]=1;
        response["errmsg"]="用户名或密码错误";
        conn->send(response.dump());
    }
    LOG_INFO<<"do login service!";
}

//处理注册业务
void ChatService::reg(const TcpConnectionPtr &conn,json &js,Timestamp time){
    string name=js["name"];
    string password=js["password"];
    User user;
    user.setName(name);
    user.setPassword(password);
    bool state=_userModel.insert(user);
    if(state){
        //注册成功
        json response;
        response["msgid"]=REG_ACK_MSG;
        response["id"]=user.getId();
        response["errno"]=0;
        conn->send(response.dump());
        
    }
    else{
        //注册失败
        json response;
        response["msgid"]=REG_ACK_MSG;
        response["errno"]=1;
        conn->send(response.dump());
    }
}

//处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn){
    
    //遍历_userConnMap表，找到连接对应的用户id
    lock_guard<mutex> lock(_connMutex);
    User user;
    for(auto it=_userConnMap.begin();it!=_userConnMap.end();++it){
        if(it->second==conn){
            //从map表删除用户的连接信息
            user.setId(it->first);
            _userConnMap.erase(it);
            //更新用户的状态信息为offline
            User user=_userModel.query(it->first);
            user.setState("offline");
            _userModel.updatestate(user);
            break;
        }
    }
}

void ChatService::onechat(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int toid=js["to"].get<int>();
    //加锁，保证_userConnMap的线程安全
    {
        lock_guard<mutex> lock(_connMutex);
        auto it=_userConnMap.find(toid);
        if(it!=_userConnMap.end()){
            //toid在线，转发消息 服务器主动推送消息给told用户
            it->second->send(js.dump());
            return;
        }
    }
    //toid不在线，存储离线消息
    _offlineMsgModel.insert(toid,js.dump());
}

//处理添加好友业务
void ChatService::addfriend(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int userid=js["id"].get<int>();
    int friendid=js["friendid"].get<int>();

    //存储好友信息
    _friendModel.insert(userid,friendid);
}
