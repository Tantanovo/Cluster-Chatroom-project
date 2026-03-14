#include"redis.hpp"
#include<iostream>
using namespace std;

redis::redis():publish_context(nullptr), subscribe_context(nullptr){

}

redis::~redis(){
    if(publish_context != nullptr){
        redisFree(publish_context);
    }
    if(subscribe_context != nullptr){
        redisFree(subscribe_context);
    }
}

bool redis::connect(){
    //负责publish发布消息的上下文连接
    publish_context = redisConnect("127.0.0.1", 6379);
    if(publish_context == nullptr){
        cerr << "connect redis failed!" << endl;
        return false;
    }
    //负责subscribe订阅消息的上下文连接
    subscribe_context = redisConnect("127.0.0.1", 6379);
    if(subscribe_context == nullptr){
        cerr << "connect redis failed!" << endl;
        return false;
    }

    //在独立线程中，监听通道上的事件,有消息就给业务层上报
    thread t([&](){
        observer_channel_message();
    });
    t.detach(); 
    cout << "connect redis-server success!" << endl;
    return true;
}

//向redis指定的通道channel发布消息
bool redis::publish(int channel, string message){
    redisReply *reply = (redisReply *)redisCommand(publish_context, "PUBLISH %d %s", channel, message.c_str());
    if(reply == nullptr){
        cerr << "publish command failed!" << endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

//在redis指定的通道subscribe订阅消息
bool redis::subscribe(int channel){
    //subscribe命令会阻塞当前上下文，这里只做订阅通道，不接收通道消息
    //通道消息的接收专门在observer_channel_message函数中独立线程中运行
    //只负责发送命令，不阻塞接收redis server响应消息，否则和notify消息线程抢占响应资源
    if(REDIS_ERR == redisAppendCommand(subscribe_context, "SUBSCRIBE %d", channel)){
        cerr << "subscribe command failed!" << endl;
        return false;
    }

    //redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕(done被置为1）
    int done = 0;
    while(!done){
        if(REDIS_ERR == redisBufferWrite(this->subscribe_context, &done)){
            cerr << "subscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

//向redis指定的通道unsubscribe取消订阅消息
bool redis::unsubscribe(int channel){
    if(REDIS_ERR == redisAppendCommand(this->subscribe_context, "UNSUBSCRIBE %d", channel)){
        cerr << "unsubscribe command failed!" << endl;
        return false;
    }
    //redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕(done被置为1）
    int done = 0;
    while(!done){
        if(REDIS_ERR == redisBufferWrite(this->subscribe_context, &done)){
            cerr << "unsubscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

//在独立线程中接收订阅通道中的消息
void redis::observer_channel_message(){
    redisReply *reply = nullptr;
    while(REDIS_OK == redisGetReply(this->subscribe_context, (void **)&reply)){
        //订阅收到的消息是一个带三元素的数组
        if(reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr){
            //给业务层上报通道上发生的消息
            _notify_message_handler(reply->element[1]->integer, reply->element[2]->str);
        }
        freeReplyObject(reply);
    }
    cerr << "observer_channel_message quit!" << endl;
}

void redis::init_notify_handler(function<void(int, string)> fn){
    this->_notify_message_handler = fn;
}