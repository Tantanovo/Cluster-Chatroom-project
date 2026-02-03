#include"json.hpp"
#include<iostream>
#include<string>
#include<thread>
#include<vector>
#include<chrono>
#include<ctime>
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

//记录当前系统登录的用户信息
User g_currentUser;
//记录当前系统登录用户的好友列表信息
vector<User> g_currentUserFriendList;
//记录当前系统登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
//显示当前登录成功用户的基本信息
void showCurrentUserData();

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
    bool isMainMenuRunning=true;//主菜单运行状态标志
    while(isMainMenuRunning){
        cout<<"======================"<<endl;
        cout<<"1. login"<<endl;
        cout<<"2. register"<<endl;
        cout<<"3. quit"<<endl;
        cout<<"======================"<<endl;
        cout<<"choice:";
        int choice=0;
        cin>>choice;
        switch (choice){
            case 1:{//登录业务
                int id=0;
                string password="";
                cout<<"userid:";
                cin>>id;
                cout<<"userpassword:";
                cin>>password;
                //组织登录json数据
                json js;    
                js["msgid"]=LOGIN_MSG;
                js["id"]=id;
                js["password"]=password;
                //发送登录数据
                string request=js.dump();
                int len=send(clientfd,request.c_str(),strlen(request.c_str())+1,0);
                if(len==-1){
                    cerr<<"send login msg error:"<<request<<endl;
                }
                else{
                    //接收服务器响应数据
                    char buffer[1024]={0};
                    int len=recv(clientfd,buffer,1024,0);
                    if(len==-1){
                        cerr<<"recv login response error!"<<endl;
                    }
                    else{
                        //解析响应数据
                        json response=json::parse(buffer);
                        if(response["errno"].get<int>()==0){
                            //登录成功
                            g_currentUser.setId(response["id"].get<int>());
                            g_currentUser.setName(response["name"]);
                            g_currentUser.setState("online");
                            //记录当前用户的好友列表信息
                            if(response.contains("friends")){
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
                                vector<string> vec=response["groups"].get<vector<string>>();
                                for(string &str:vec){
                                    json js=json::parse(str);
                                    Group group;
                                    group.setId(js["id"].get<int>());   
                                    group.setName(js["name"]);
                                    group.setDesc(js["desc"]);
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
                            //启动接收线程
                            thread readTask(readTaskHandler,clientfd);
                            readTask.detach();
                            //进入主聊天页面
                            mainMenu(clientfd);
                            isMainMenuRunning=false;//结束主菜单循环
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
                string request=js.dump();
                int len=send(clientfd,request.c_str(),strlen(request.c_str())+1,0);
                if(len==-1){
                    cerr<<"send reg msg error:"<<request<<endl;
                }
                else{
                    //接收服务器响应数据
                    char buffer[1024]={0};
                    int len=recv(clientfd,buffer,1024,0);
                    if(len==-1){
                        cerr<<"recv reg response error!"<<endl; 
                    }
                    else{
                        //解析响应数据
                        json response=json::parse(buffer);
                        if(response["errno"].get<int>()==0){
                            //注册成功
                            cout<<"register success! userid is "<<response["id"]<<", please remember it!"<<endl;
                        }
                        else{
                            //注册失败
                            cerr<<"register failed! errno is "<<response["errno"].get<int>()<<endl;
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
        char buffer[1024]={0};
        int len=recv(clientfd,buffer,1024,0);
        if(len==-1||len==0){
            close(clientfd);
            exit(-1);
        }

        json js=json::parse(buffer);
        if(ONE_CHAT_MSG==js["msgid"].get<int>()){
            cout<<js["time"].get<string>()<<"["<<js["id"]<<"]"<<js["name"].get<string>()<<"said:"<<js["msg"].get<string>()<<endl;
            continue;
        }
    }   
}
//显示当前登录成功用户的基本信息
void showCurrentUserData(){
    // TODO: 实现显示当前用户信息的功能
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
//"quit" commend handler
void quit(int,string);

//系统支持的客户端命令列表
unordered_map<string,string>commandMap={
    {"help","显示所有支持的命令,格式help"},
    {"chat","一对一聊天,格式chat:friendid:message"},
    {"addfriend","添加好友,格式addfriend:friendid"},
    {"creategroup","创建群组,格式creategroup:groupname:groupdesc"},
    {"addgroup","加入群组,格式addgroup:groupid"},
    {"groupchat","群聊,格式groupchat:groupid:message"},
    {"quit","注销,格式quit"}
};

//注册系统支持的客户端命令处理
unordered_map<string,function<void(int,string)>> commandHandlerMap={
    {"help",help},
    {"chat",chat},
    {"addfriend",addfriend},
    {"creategroup",creategroup},
    {"addgroup",addgroup},
    {"groupchat",groupchat},
    {"quit",quit}
};

//主聊天页面程序
void mainMenu(int clientfd){
    help();

    char buffer[1024]={0};
    for(;;){
        cin.getline(buffer,1024);
        string commandbuf(buffer);
        string command;//存储命令
        int idx=commandbuf.find(":");
        if(idx==-1){
            command=commandbuf;
        }
        else{
            command=commandbuf.substr(0,idx);
        }
        auto it=commandHandlerMap.find(command);
        if(it==commandHandlerMap.end()){
            cerr<<"invalid input command!"<<endl;
            continue;
        }
        //调用相应命令的事件处理回调，mainmenu对修改封闭，添加新功能不需要修改该函数
        it->second(clientfd,commandbuf.substr(idx+1,commandbuf.size()-idx));//调用命令处理方法
        
    }
}
