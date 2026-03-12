#include"chatserver.hpp"
#include"chatservice.hpp"
#include<iostream>
#include<signal.h>
using namespace std;
//处理服务器ctrl+c结束后，重置user的状态信息
void resetHandler(int){
    ChatService::instance()->reset();
    exit(0);
}
int main(){
    signal(SIGPIPE,resetHandler);//重置信号处理函数，防止服务器崩溃

    EventLoop loop;//创建事件循环对象
    InetAddress addr("127.0.0.1",6000);//设置服务器端口号
    ChatServer server(&loop,addr,"ChatServer");//创建服务器对象
    server.start();//启动服务器
    loop.loop();//启动事件循环
    return 0;

}