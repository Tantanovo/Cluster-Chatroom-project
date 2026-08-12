#include"redis.hpp"
#include<iostream>
#include<muduo/base/Logging.h>
using namespace std;

redis::redis():publish_context(nullptr), subscribe_context(nullptr), command_context(nullptr){

}

redis::~redis(){
    if(publish_context != nullptr){
        redisFree(publish_context);
    }
    if(subscribe_context != nullptr){
        redisFree(subscribe_context);
    }
    if(command_context != nullptr){
        redisFree(command_context);
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
    //专用于普通命令(INCR取号)的上下文连接
    //必须独立：订阅连接处于订阅态被observer线程独占，publish连接被多线程并发写
    command_context = redisConnect("127.0.0.1", 6379);
    if(command_context == nullptr){
        cerr << "connect redis(command) failed!" << endl;
        return false;
    }

    //在独立线程中，监听通道上的事件,有消息就给业务层上报
    thread t([&](){
        observer_channel_message();
    });
    t.detach(); 
    _redisAvailable.store(true);
    cout << "connect redis-server success!" << endl;
    return true;
}

//向redis指定的通道channel发布消息
bool redis::publish(int channel, string message){
    lock_guard<mutex> lock(_pubMutex);
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

//====================== 会话级消息序号 ======================

//为会话原子分配下一个递增序号
//实现要点：INCR 是 Redis 单线程内的原子操作，多节点并发调用不会拿到相同的号，
//         因此无需分布式锁，也不存在 read-modify-write 竞态。
uint64_t redis::incrSeq(const string &conversationId){
    if(command_context == nullptr || !_redisAvailable.load()){
        return localFallbackSeq(conversationId);
    }

    string key = "chat:seq:" + conversationId;
    uint64_t seq = 0;
    {
        //4个IO线程会并发取号，必须串行化对 command_context 的使用，
        //否则并发 redisCommand 会导致响应包错位（拿到别人的回包）。
        lock_guard<mutex> lock(_cmdMutex);
        redisReply *reply = (redisReply *)redisCommand(command_context, "INCR %s", key.c_str());
        if(reply == nullptr){
            //连接异常：标记不可用并降级，避免每条消息都卡在超时上
            LOG_ERROR << "INCR failed, redis unavailable, fallback to local seq. key=" << key;
            _redisAvailable.store(false);
            return localFallbackSeq(conversationId);
        }
        if(reply->type == REDIS_REPLY_INTEGER){
            seq = static_cast<uint64_t>(reply->integer);
        }
        freeReplyObject(reply);
    }

    if(seq == 0){
        LOG_ERROR << "INCR returned unexpected type, fallback. key=" << key;
        return localFallbackSeq(conversationId);
    }
    return seq;
}

//查询会话当前序号（不递增）
uint64_t redis::getSeq(const string &conversationId){
    if(command_context == nullptr || !_redisAvailable.load())return 0;

    string key = "chat:seq:" + conversationId;
    uint64_t seq = 0;
    lock_guard<mutex> lock(_cmdMutex);
    redisReply *reply = (redisReply *)redisCommand(command_context, "GET %s", key.c_str());
    if(reply == nullptr){
        _redisAvailable.store(false);
        return 0;
    }
    if(reply->type == REDIS_REPLY_STRING && reply->str != nullptr){
        seq = strtoull(reply->str, nullptr, 10);
    }
    freeReplyObject(reply);
    return seq;
}

//降级取号：Redis 不可用时保证业务不中断。
//加 1<<40 高位偏移，让降级号段与正常 Redis 号段不重叠，
//便于事后排查，也避免 Redis 恢复后新号小于降级号导致客户端误判重复。
uint64_t redis::localFallbackSeq(const string &conversationId){
    static const uint64_t kFallbackBase = 1ULL << 40;
    lock_guard<mutex> lock(_localSeqMutex);
    uint64_t &v = _localSeq[conversationId];
    ++v;
    return kFallbackBase + v;
}
