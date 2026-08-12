#include"json.hpp"
#include<iostream>
#include<string>
#include<thread>
#include<vector>
#include<chrono>
#include<ctime>
#include<limits>
#include<mutex>
using namespace std;
using json=nlohmann::json;

#include<unistd.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include"group.hpp"
#include"user.hpp"
#include"public.hpp"

//=================== 协议层：与服务器保持一致 ===================
//帧格式：[4字节网络序长度头][JSON body]
//改造前客户端直接send裸JSON，与服务器一样存在粘包/半包问题。
//现在统一封装 sendFrame / recvFrame。
//---------------------------------------------------------------
static const size_t kHeaderLen=4;
static const uint32_t kMaxFrameLen=64*1024;

//发送一帧：序列化json -> 加长度头 -> 一次sendall
bool sendFrame(int clientfd,const json &js){
    string body=js.dump();
    uint32_t beLen=htonl(static_cast<uint32_t>(body.size()));
    string frame;
    frame.reserve(kHeaderLen+body.size());
    frame.append(reinterpret_cast<const char*>(&beLen),kHeaderLen);
    frame.append(body);
    size_t sent=0;
    while(sent<frame.size()){
        ssize_t n=send(clientfd,frame.data()+sent,frame.size()-sent,0);
        if(n<=0)return false;
        sent+=static_cast<size_t>(n);
    }
    return true;
}

//接收缓冲：处理粘包/半包（一条recv可能含多帧或半帧）
string g_recvBuf;
mutex g_recvMutex;

//读取一帧完整body，返回false表示连接断开或帧非法
bool recvFrame(int clientfd,string &body){
    lock_guard<mutex> lock(g_recvMutex);
    for(;;){
        //缓冲区内已有完整帧？先处理
        if(g_recvBuf.size()>=kHeaderLen){
            uint32_t beLen=0;
            memcpy(&beLen,g_recvBuf.data(),kHeaderLen);
            const uint32_t bodyLen=ntohl(beLen);
            if(bodyLen==0||bodyLen>kMaxFrameLen){
                g_recvBuf.clear();
                return false;
            }
            if(g_recvBuf.size()>=kHeaderLen+bodyLen){
                body.assign(g_recvBuf.data()+kHeaderLen,bodyLen);
                g_recvBuf.erase(0,kHeaderLen+bodyLen);
                return true;
            }
        }
        //缓冲区不足一帧，继续recv
        char tmp[4096];
        ssize_t n=recv(clientfd,tmp,sizeof(tmp),0);
        if(n<=0)return false;
        g_recvBuf.append(tmp,static_cast<size_t>(n));
    }
}

//=================== 业务逻辑 ===================
//记录当前系统登录的用户信息
User g_currentUser;
//记录当前系统登录用户的好友列表信息
vector<User> g_currentUserFriendList;
//记录当前系统登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
//显示当前登录成功用户的基本信息
void showCurrentUserData();
//控制主菜单页面程序
bool isMainMenuRunning=true;

//接收线程
void readTaskHandler(int clientfd);
//获取系统时间（聊天信息添加时间戳）
string getCurrentTime();
//主聊天页面程序
void mainMenu(int clientfd);

//聊天客户端程序实现，main线程用作发送线程 子线程用作接收线程
int main(int argc,char **argv){
    if(argc<3){
        cerr<<"command invalid! example: ./chatclient 127.0.0.1 6000"<<endl;
        exit(-1);
    }
    //解析获取服务器端的ip和端口号
    char* ip=argv[1];
    uint16_t port=atoi(argv[2]);
    //创建客户端的socket
    int clientfd=socket(AF_INET,SOCK_STREAM,0);
    if(clientfd==-1){
        cerr<<"socket create error!"<<endl;
        exit(-1);
    }
    //填写客户端需要连接的服务器信息
    struct sockaddr_in serveraddr;
    memset(&serveraddr,0,sizeof(serveraddr));
    serveraddr.sin_family=AF_INET;
    serveraddr.sin_port=htons(port);
    serveraddr.sin_addr.s_addr=inet_addr(ip);
    //连接服务器
    if(connect(clientfd,(struct sockaddr*)&serveraddr,sizeof(serveraddr))==-1){
        cerr<<"connect server error!"<<endl;
        close(clientfd);
        exit(-1);
    }
    cout<<"connect server success!"<<endl;
    //登录注册主菜单
    while(isMainMenuRunning){
        cout<<"======================"<<endl;
        cout<<"1. login"<<endl;
        cout<<"2. register"<<endl;
        cout<<"3. quit"<<endl;
        cout<<"======================"<<endl;
        cout<<"choice:";
        int choice=0;
        cin>>choice;
        if(cin.fail()){
            //输入的不是数字，清除错误状态并丢弃缓冲区残留内容，避免死循环
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(),'\n');
            cerr<<"invalid input!"<<endl;
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(),'\n');
        switch (choice){
            case 1:{//登录业务
                string name="";
                string password="";
                cout<<"username:";
                cin>>name;
                cout<<"userpassword:";
                cin>>password;
                //组织登录json数据
                json js;
                js["msgid"]=LOGIN_MSG;
                js["name"]=name;
                js["password"]=password;
                //发送登录数据
                if(!sendFrame(clientfd,js)){
                    cerr<<"send login msg error!"<<endl;
                }
                else{
                    //接收服务器响应数据
                    string responseStr;
                    if(!recvFrame(clientfd,responseStr)){
                        cerr<<"recv login response error!"<<endl;
                    }
                    else{
                        //解析响应数据
                        json response=json::parse(responseStr);
                        if(response["errno"].get<int>()==0){
                            //登录成功
                            g_currentUser.setId(response["id"].get<int>());
                            g_currentUser.setName(response["name"]);
                            g_currentUser.setState("online");
                            //记录当前用户的好友列表信息
                            if(response.contains("friends")){
                                //初始化
                                g_currentUserFriendList.clear();
                                vector<string> vec=response["friends"].get<vector<string>>();
                                for(string &str:vec){
                                    json js=json::parse(str);
                                    User user;
                                    user.setId(js["id"].get<int>());
                                    user.setName(js["name"]);
                                    user.setState(js["state"]);
                                    g_currentUserFriendList.push_back(user);
                                }
                            }
                            //记录当前用户的群组列表信息
                            if(response.contains("groups")){
                                //初始化
                                g_currentUserGroupList.clear();
                                vector<string> vec=response["groups"].get<vector<string>>();
                                for(string &str:vec){
                                    json js=json::parse(str);
                                    Group group;
                                    group.setId(js["id"].get<int>());
                                    group.setName(js["groupname"]);
                                    group.setDesc(js["groupdesc"]);
                                    //记录群组的成员信息
                                    if(js.contains("users")){
                                        vector<string> vec2=js["users"].get<vector<string>>();
                                        for(string &userstr:vec2){
                                            json js2=json::parse(userstr);
                                            Groupuser groupuser;
                                            groupuser.setId(js2["id"].get<int>());
                                            groupuser.setName(js2["name"]);
                                            groupuser.setState(js2["state"]);
                                            groupuser.setRole(js2["role"]);
                                            group.getUsers().push_back(groupuser);
                                        }
                                    }
                                    g_currentUserGroupList.push_back(group);
                                }
                            }
                            //显示当前用户的基本信息
                            showCurrentUserData();

                            //显示当前用户的离线消息  个人聊天信息或者群组消息
                            if(response.contains("offlinemsg")){
                                vector<string> vec=response["offlinemsg"].get<vector<string>>();
                                for(string &str:vec){
                                    json js=json::parse(str);
                                    if(ONE_CHAT_MSG==js["msgid"].get<int>()){
                                        cout<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"].get<string>()<<"said:"<<js["msg"].get<string>()<<endl;
                                    }
                                    else if(GROUP_CHAT_MSG==js["msgid"].get<int>()){
                                        cout<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"].get<string>()<<"in group["<<js["groupid"].get<int>()<<"]said:"<<js["msg"].get<string>()<<endl;
                                    }
                                }
                            }
                            //登陆成功，启动接收线程负责接收数据 该线程只启动一次
                            static int threadnumber=0;
                            if(threadnumber==0){
                                thread readTask(readTaskHandler,clientfd);
                                readTask.detach();
                                threadnumber++;
                            }
                            //进入主聊天页面
                            isMainMenuRunning=true;
                            mainMenu(clientfd);
                        }
                        else{
                            //登录失败
                            cerr<<response["errmsg"]<<endl;
                        }
                    }
                }
                break;
            }
            case 2:{//注册业务
                string name="";
                string password="";
                cout<<"username:";
                cin>>name;
                cout<<"userpassword:";
                cin>>password;
                //组织注册json数据
                json js;
                js["msgid"]=REG_MSG;
                js["name"]=name;
                js["password"]=password;
                //发送注册数据
                if(!sendFrame(clientfd,js)){
                    cerr<<"send reg msg error!"<<endl;
                }
                else{
                    //接收服务器响应数据
                    string responseStr;
                    if(!recvFrame(clientfd,responseStr)){
                        cerr<<"recv reg response error!"<<endl;
                    }
                    else{
                        //解析响应数据
                        json response=json::parse(responseStr);
                        if(response["errno"].get<int>()==0){
                            //注册成功
                            cout<<"register success! you can login with username \""<<name<<"\" now."<<endl;
                        }
                        else{
                            //注册失败（多为用户名已被占用）
                            cerr<<"register failed! username \""<<name<<"\" may already exist, please try another."<<endl;
                        }
                    }
                }
                break;
            }
            case 3:{//退出程序
                close(clientfd);
                isMainMenuRunning=false;
                break;
            }
            default:
                cerr<<"invalid input!"<<endl;
                break;
        }
    }
    return 0;
}

//接收线程
void readTaskHandler(int clientfd){
    for(;;){
        string body;
        if(!recvFrame(clientfd,body)){
            close(clientfd);
            exit(-1);
        }
        //接收chatserver转发的数据，反序列化生成json数据对象
        json js=json::parse(body);
        int msgtype=js["msgid"].get<int>();
        if(ONE_CHAT_MSG==msgtype){
            cout<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"].get<string>()<<"said:"<<js["msg"].get<string>()<<endl;
            continue;
        }
        else if(GROUP_CHAT_MSG==msgtype){
            cout<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"].get<string>()<<"in group["<<js["groupid"].get<int>()<<"]said:"<<js["msg"].get<string>()<<endl;
            continue;
        }
        else if(LOGINOUT_MSG==msgtype){
            if(js["id"].get<int>()==g_currentUser.getId()){
                cout<<"you are loginout success!"<<endl;
                g_currentUser.setState("offline");
                continue;
            }
            else{
                cout<<js["name"].get<string>()<<"is loginout success!"<<endl;
                continue;
            }
        }
        else{
            cout<<"recv unknown msgtype:"<<msgtype<<endl;
        }
    }
}
//显示当前登录成功用户的基本信息
void showCurrentUserData(){
    cout<<"==================login user================="<<endl;
    cout<<"current login user => id:: "<<g_currentUser.getId()<<endl;
    cout<<"current login user => name:: "<<g_currentUser.getName()<<endl;
    cout<<"current login user => state:: "<<g_currentUser.getState()<<endl;
    cout<<"------------------friend list-----------------"<<endl;
    if(!g_currentUserFriendList.empty()){
        for(User &user:g_currentUserFriendList){
           cout<<user.getId()<<":"<<user.getName()<<":"<<user.getState()<<endl;
        }
    }
    else{
        cout<<"you have no friend!"<<endl;
    }
    cout<<"-----------------group list-----------------"<<endl;
    if(!g_currentUserGroupList.empty()){
        for(Group &group:g_currentUserGroupList){
            cout<<group.getId()<<":"<<group.getName()<<":"<<group.getDesc()<<endl;
            for(Groupuser &user:group.getUsers()){
               cout<<user.getId()<<":"<<user.getName()<<":"<<user.getState()<<":"<<user.getRole()<<endl;
            }
        }
    }
    else{
        cout<<"you have no group!"<<endl;
    }
    cout<<"================================================="<<endl;
}

//"help" commend handler
void help(int fd=0,string str="");
//"chat" commend handler
void chat(int,string);
//"addfrind" commend handler
void addfriend(int,string);
//"creategroup" commend handler
void creategroup(int,string);
//"addgroup" commend handler
void addgroup(int,string);
//"groupchat" commend handler
void groupchat(int,string);
//"loginout" commend handler
void loginout(int,string);

//系统支持的客户端命令列表
unordered_map<string,string>commandMap={
    {"help","显示所有支持的命令,格式help"},
    {"chat","一对一聊天,格式chat:friendid:message"},
    {"addfriend","添加好友,格式addfriend:friendid"},
    {"creategroup","创建群组,格式creategroup:groupname:groupdesc"},
    {"addgroup","加入群组,格式addgroup:groupid"},
    {"groupchat","群聊,格式groupchat:groupid:message"},
    {"loginout","注销,格式loginout"}
};

//注册系统支持的客户端命令处理
unordered_map<string,function<void(int,string)>> commandHandlerMap={
    {"help",help},
    {"chat",chat},
    {"addfriend",addfriend},
    {"creategroup",creategroup},
    {"addgroup",addgroup},
    {"groupchat",groupchat},
    {"loginout",loginout}
};

//主聊天页面程序
void mainMenu(int clientfd){
    help();

    char buffer[1024]={0};
    for(;;){
        cin.getline(buffer,1024);
        string commandbuf(buffer);
        if(commandbuf.empty())continue;//忽略空行
        string command;//存储命令
        string args;//存储命令参数
        int idx=commandbuf.find(":");
        if(idx==-1){
            command=commandbuf;
            args="";
        }
        else{
            command=commandbuf.substr(0,idx);
            args=commandbuf.substr(idx+1,commandbuf.size()-idx);
        }
        auto it=commandHandlerMap.find(command);
        if(it==commandHandlerMap.end()){
            cerr<<"invalid input command!"<<endl;
            continue;
        }
        //调用相应命令的事件处理回调，mainmenu对修改封闭，添加新功能不需要修改该函数
        it->second(clientfd,args);//调用命令处理方法

    }
}

void help(int,string){
    cout<<"show command list>>>"<<endl;
    for(auto &p:commandMap){
        cout<<p.first<<":"<<p.second<<endl;
    }
    cout<<endl;
}

void addfriend(int clientfd,string str){
    if(str.empty()){
        cerr<<"addfriend command format error! example: addfriend:friendid"<<endl;
        return;
    }
    int friendid=0;
    try{
        friendid=stoi(str);
    }catch(...){
        cerr<<"addfriend command format error! friendid must be a number"<<endl;
        return;
    }
    json js;
    js["msgid"]=ADD_FRIEND_MSG;
    js["id"]=g_currentUser.getId();
    js["friendid"]=friendid;
    if(!sendFrame(clientfd,js)){
        cerr<<"send addfriend msg error!"<<endl;
    }
}

void chat(int clientfd,string str){
    int idx=str.find(":");
    if(idx==-1){
        cerr<<"chat command format error!"<<endl;
        return;
    }
    int friendid=atoi(str.substr(0,idx).c_str());
    string message=str.substr(idx+1,str.size()-idx);
    json js;
    js["msgid"]=ONE_CHAT_MSG;
    js["id"]=g_currentUser.getId();
    js["name"]=g_currentUser.getName();
    js["toid"]=friendid;
    js["msg"]=message;
    js["time"]=getCurrentTime();
    if(!sendFrame(clientfd,js)){
        cerr<<"send chat msg error!"<<endl;
    }
}

void creategroup(int clientfd,string str){
    int idx=str.find(":");
    if(idx==-1){
        cerr<<"creategroup command format error!"<<endl;
        return;
    }
    string groupname=str.substr(0,idx);
    string groupdesc=str.substr(idx+1,str.size()-idx);
    json js;
    js["msgid"]=CREATE_GROUP_MSG;
    js["id"]=g_currentUser.getId();
    js["groupname"]=groupname;
    js["groupdesc"]=groupdesc;
    if(!sendFrame(clientfd,js)){
        cerr<<"send creategroup msg error!"<<endl;
    }
}
void addgroup(int clientfd,string str){
    if(str.empty()){
        cerr<<"addgroup command format error! example: addgroup:groupid"<<endl;
        return;
    }
    int groupid=0;
    try{
        groupid=stoi(str);
    }catch(...){
        cerr<<"addgroup command format error! groupid must be a number"<<endl;
        return;
    }
    json js;
    js["msgid"]=ADD_GROUP_MSG;
    js["id"]=g_currentUser.getId();
    js["groupid"]=groupid;
    if(!sendFrame(clientfd,js)){
        cerr<<"send addgroup msg error!"<<endl;
    }
}

void groupchat(int clientfd,string str){
    int idx1=str.find(":");
    if(idx1==-1){
        cerr<<"groupchat command format error!"<<endl;
        return;
    }
    int groupid=0;
    try{
        groupid=stoi(str.substr(0,idx1));
    }catch(...){
        cerr<<"groupchat command format error! groupid must be a number"<<endl;
        return;
    }
    string message=str.substr(idx1+1,str.size()-idx1);
    json js;
    js["msgid"]=GROUP_CHAT_MSG;
    js["id"]=g_currentUser.getId();
    js["name"]=g_currentUser.getName();
    js["groupid"]=groupid;
    js["msg"]=message;
    js["time"]=getCurrentTime();
    if(!sendFrame(clientfd,js)){
        cerr<<"send groupchat msg error!"<<endl;
    }
}

void loginout(int clientfd,string str){
    json js;
    js["msgid"]=LOGINOUT_MSG;
    js["id"]=g_currentUser.getId();
    if(!sendFrame(clientfd,js)){
        cerr<<"send loginout msg error!"<<endl;
    }
    else{
        isMainMenuRunning=false;
    }
}

string getCurrentTime(){
    auto tt=chrono::system_clock::to_time_t(chrono::system_clock::now());
    struct tm *ptm=localtime(&tt);
    char date[60]={0};
    sprintf(date,"%d-%02d-%02d %02d:%02d:%02d",
        (1900+(int)ptm->tm_year),(1+(int)ptm->tm_mon),ptm->tm_mday,
        ptm->tm_hour,ptm->tm_min,ptm->tm_sec);
    return string(date);
}
