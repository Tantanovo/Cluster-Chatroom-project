#include"chatserver.hpp"
#include<iostream>
using namespace std;
int main(){
    EventLoop loop;//创建事件循环对象
    InetAddress addr("127.0.0.1",6000);//设置服务器端口号
    ChatServer server(&loop,addr,"ChatServer");//创建服务器对象
    server.start();//启动服务器
    loop.loop();//启动事件循环
    return 0;

}