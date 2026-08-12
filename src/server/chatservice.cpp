#include"chatservice.hpp"
#include"chatserver.hpp"
#include"public.hpp"
#include"conversation.hpp"
#include"crypto.hpp"
#include<string>
#include<muduo/base/Logging.h>
#include<vector>
#include<mutex>
#include<algorithm>
using namespace std;
using namespace muduo;

ChatService* ChatService::instance(){
    static ChatService service;
    return &service;
}

ChatService::ChatService(){
    //注册消息以及对应的Handler

    //用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG,bind(&ChatService::login,this,_1,_2,_3)});
    _msgHandlerMap.insert({LOGINOUT_MSG,bind(&ChatService::loginout,this,_1,_2,_3)});
    _msgHandlerMap.insert({REG_MSG,bind(&ChatService::reg,this,_1,_2,_3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG,bind(&ChatService::onechat,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG,bind(&ChatService::addfriend,this,_1,_2,_3)});

    //群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG,bind(&ChatService::creategroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG,bind(&ChatService::addgroup,this,_1,_2,_3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG,bind(&ChatService::groupchat,this,_1,_2,_3)});

    if(_redis.connect()){
        //初始化消息回调
        _redis.init_notify_handler(bind(&ChatService::handleredissubscribemessage,this,_1,_2));
    }
}

void ChatService::reset(){
    //把online状态的用户，设置成offline（服务器优雅退出时调用）
    _userModel.resetState();
}

//把离线消息按 (convid, seq) 升序排列。
//解析失败或缺字段的旧消息排到最后，保证兼容改造前入库的历史数据。
void ChatService::sortOfflineMessages(vector<string> &msgs){
    struct Item{ string raw; string conv; uint64_t seq; bool ok; };
    vector<Item> items;
    items.reserve(msgs.size());
    for(size_t i=0;i<msgs.size();++i){
        Item it{msgs[i],"",0,false};
        try{
            json j=json::parse(msgs[i]);
            if(j.contains(MSG_FIELD_SEQ)&&j[MSG_FIELD_SEQ].is_number_unsigned()){
                it.seq=j[MSG_FIELD_SEQ].get<uint64_t>();
                it.conv=j.contains(MSG_FIELD_CONVID)?j[MSG_FIELD_CONVID].get<string>():"";
                it.ok=true;
            }
        }catch(const std::exception &){
            //非法JSON：保持原样，排到末尾
        }
        items.push_back(std::move(it));
    }
    stable_sort(items.begin(),items.end(),[](const Item &a,const Item &b){
        if(a.ok!=b.ok)return a.ok;              //有序号的排前面
        if(!a.ok&&!b.ok)return false;           //都没序号：维持原有相对顺序
        if(a.conv!=b.conv)return a.conv<b.conv; //先按会话分组
        return a.seq<b.seq;                     //会话内按序号升序
    });
    for(size_t i=0;i<items.size();++i)msgs[i]=items[i].raw;
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
    string name=js["name"];
    string password=js["password"];
    User user=_userModel.queryByName(name);
    int id=user.getId();
    //改造前：直接比较 user.getPassword()==password（明文）
    //改造后：比对 SHA-256(salt+password) 与库中哈希
    if(id!=-1&&crypto::safeEqual(crypto::hashPassword(user.getSalt(),password),
                                  user.getPassword())){
        if(user.getState()=="online"){
            //该用户已经登录，不能重复登录
            json response;
            response["msgid"]=LOGIN_ACK_MSG;
            response["errno"]=2;
            response["errmsg"]="该账号已经登录，不能重复登录";
            ChatServer::sendWithHeader(conn,response.dump());
            return;
        }
        else{
            //登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id,conn});
            }
            //将用户id和通道号绑定，用于后续的消息推送
            _redis.subscribe(id);

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
                //【有序性】离线消息的入库顺序不等于会话内的seq顺序：
                //并发写入、跨节点降级落库都可能让物理行序与逻辑序号不一致。
                //这里在下发前按 (convid, seq) 排序，客户端拿到即是正确顺序。
                sortOfflineMessages(vec);
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

            //查询用户的群组信息
            vector<Group> GroupuserVec=_groupModel.queryGroups(id);
            if(!GroupuserVec.empty()){
                vector<string> vec2;
                for(Group &group:GroupuserVec){
                    json grpjson;
                    grpjson["id"]=group.getId();
                    grpjson["groupname"]=group.getName();
                    grpjson["groupdesc"]=group.getDesc();
                    vector<string>userV;
                    for(Groupuser &user:group.getUsers()){
                        json userjson;
                        userjson["id"]=user.getId();
                        userjson["name"]=user.getName();
                        userjson["state"]=user.getState();
                        userjson["role"]=user.getRole();
                        userV.push_back(userjson.dump());
                    }
                    grpjson["users"]=userV;
                    vec2.push_back(grpjson.dump());
                }
                response["groups"]=vec2;
            }
            ChatServer::sendWithHeader(conn,response.dump());
        }
    }
    else{
        //该用户不存在,登录失败
        json response;
        response["msgid"]=LOGIN_ACK_MSG;
        response["errno"]=1;
        response["errmsg"]="用户名或密码错误";
        ChatServer::sendWithHeader(conn,response.dump());
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
        ChatServer::sendWithHeader(conn,response.dump());

    }
    else{
        //注册失败
        json response;
        response["msgid"]=REG_ACK_MSG;
        response["errno"]=1;
        ChatServer::sendWithHeader(conn,response.dump());
    }
}

//处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int userid=js["id"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it=_userConnMap.find(userid);
        if(it!=_userConnMap.end()){
            //从map表删除用户的连接信息
            _userConnMap.erase(it);
        }
    }
    //取消订阅通道
    _redis.unsubscribe(userid);

    //更新用户的状态信息为offline
    User user(userid,"","offline");
    _userModel.updatestate(user);
}

//处理客户端异常退出（连接断开时由网络层回调触发）
void ChatService::clientCloseException(const TcpConnectionPtr &conn){
    int userid=-1;
    {
        lock_guard<mutex> lock(_connMutex);
        //遍历_userConnMap表，找到连接对应的用户id
        for(auto it=_userConnMap.begin();it!=_userConnMap.end();++it){
            if(it->second==conn){
                userid=it->first;
                _userConnMap.erase(it);
                break;
            }
        }
    }
    if(userid==-1)return;//该连接不是登录用户的连接，无需清理

    //更新用户的状态信息为offline
    User user=_userModel.query(userid);
    if(user.getId()!=-1){
        user.setState("offline");
        _userModel.updatestate(user);
    }
    //取消订阅通道
    _redis.unsubscribe(userid);
    LOG_INFO<<"client close, userid="<<userid<<" set offline";
}

//处理一对一聊天业务
void ChatService::onechat(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int fromid=js["id"].get<int>();
    int toid=js["toid"].get<int>();

    //====================== 消息有序性：先取号，再投递 ======================
    //【为什么必须在分支之前取号】
    //下面有三条投递路径：本节点直发 / Redis跨节点转发 / 落库离线。
    //如果在各分支内分别取号，同一会话的连续两条消息若走了不同路径，
    //就会出现"后发的先取到号"或号段错乱。统一在入口取号，保证
    //序号的分配顺序严格等于服务端接收顺序。
    const string convId=conv::p2p(fromid,toid);
    const uint64_t seq=_redis.incrSeq(convId);
    js[MSG_FIELD_CONVID]=convId;
    js[MSG_FIELD_SEQ]=seq;
    js[MSG_FIELD_TS]=static_cast<int64_t>(time.microSecondsSinceEpoch()/1000);
    const string payload=js.dump();//取号后固化，三条路径发的是同一份数据

    //加锁，保证_userConnMap的线程安全
    {
        lock_guard<mutex> lock(_connMutex);
        auto it=_userConnMap.find(toid);
        if(it!=_userConnMap.end()){
            //toid在本节点在线，服务器主动推送消息给toid用户
            ChatServer::sendWithHeader(it->second,payload);
            return;
        }
    }
    //查询toid是否在线
    User user=_userModel.query(toid);
    if(user.getState()=="online"){
        //toid在线但在其他服务器节点，通过redis发布订阅转发
        //【注意】Pub/Sub不持久化，publish失败或对端瞬时离线都会丢消息，
        //所以失败时降级落库，由对方登录时补投（配合seq可让客户端补洞）
        if(!_redis.publish(toid,payload)){
            LOG_ERROR<<"publish failed, fallback to offline store. toid="<<toid;
            _offlineMsgModel.insert(toid,payload);
        }
        return;
    }
    //toid不在线，存储离线消息
    _offlineMsgModel.insert(toid,payload);
}

//处理添加好友业务
void ChatService::addfriend(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int userid=js["id"].get<int>();
    int friendid=js["friendid"].get<int>();

    //存储好友信息
    _friendModel.insert(userid,friendid);
}

//创建群组业务
void ChatService::creategroup(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int userid=js["id"].get<int>();
    string groupname=js["groupname"];
    string groupdesc=js["groupdesc"];
    //改造前：先createGroup再addGroup，两条独立语句，第二条失败会留下孤儿群组
    //改造后：两条insert包在同一个事务里，失败整体回滚
    Group group(-1,groupname,groupdesc);
    _groupModel.createGroupWithCreator(group,userid);
}

//加入群组业务
void ChatService::addgroup(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int userid=js["id"].get<int>();
    int groupid=js["groupid"].get<int>();
    _groupModel.addGroup(userid,groupid,"normal");
}

//群组聊天业务
void ChatService::groupchat(const TcpConnectionPtr &conn,json &js,Timestamp time){
    int userid=js["id"].get<int>();
    int groupid=js["groupid"].get<int>();

    //====================== 群聊同样先取号 ======================
    //群内所有成员共享同一个会话序号，保证每个成员看到的群消息顺序完全一致。
    const string convId=conv::group(groupid);
    const uint64_t seq=_redis.incrSeq(convId);
    js[MSG_FIELD_CONVID]=convId;
    js[MSG_FIELD_SEQ]=seq;
    js[MSG_FIELD_TS]=static_cast<int64_t>(time.microSecondsSinceEpoch()/1000);
    const string payload=js.dump();

    //查询群组成员列表，除userid自己外，其他成员转发消息
    vector<int> useridVec=_groupModel.queryGroupUsers(userid,groupid);

    //【性能修正】改造前是"持锁遍历成员并send"，群成员多时锁持有时间很长，
    //会阻塞其他IO线程的登录/登出/单聊。现在锁内只做一件事：把目标连接的
    //shared_ptr 拷出来（TcpConnectionPtr 是 shared_ptr，拷贝后能保证
    //send 期间对象不被析构），真正的发送与DB查询都放到锁外做。
    vector<TcpConnectionPtr> localConns;   //本节点在线的连接
    vector<int> remoteCandidates;          //不在本节点，待判定在线状态
    {
        lock_guard<mutex> lock(_connMutex);
        localConns.reserve(useridVec.size());
        for(int id:useridVec){
            auto it=_userConnMap.find(id);
            if(it!=_userConnMap.end()){
                localConns.push_back(it->second);
            }else{
                remoteCandidates.push_back(id);
            }
        }
    }

    //锁外发送：本节点成员直接推送
    for(const auto &c:localConns){
        ChatServer::sendWithHeader(c,payload);
    }

    //锁外处理：跨节点转发或落库
    for(int id:remoteCandidates){
        User user=_userModel.query(id);
        if(user.getState()=="online"){
            //在其他节点在线，redis转发；失败则降级落库，避免消息凭空消失
            if(!_redis.publish(id,payload)){
                LOG_ERROR<<"group publish failed, fallback offline. userid="<<id;
                _offlineMsgModel.insert(id,payload);
            }
        }
        else{
            //不在线，存储离线消息
            _offlineMsgModel.insert(id,payload);
        }
    }
}

//从redis订阅通道收到跨服务器消息，投递给本节点的目标用户
//【注意】消息里已带 convid/seq（由发送节点分配），这里只做透传，
//绝不重新取号——重新取号会导致同一条消息在不同节点有不同序号。
void ChatService::handleredissubscribemessage(int userid,string msg){
    TcpConnectionPtr target;
    {
        lock_guard<mutex> lock(_connMutex);
        auto it=_userConnMap.find(userid);
        if(it!=_userConnMap.end()){
            target=it->second;//锁内只拷贝shared_ptr
        }
    }
    if(target!=nullptr){
        //锁外发送，避免网络写阻塞其他线程
        ChatServer::sendWithHeader(target,msg);
        return;
    }
    //目标用户恰好不在本节点（如刚好断开），存储离线消息
    _offlineMsgModel.insert(userid,msg);
}
