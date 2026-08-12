#ifndef __REDIS_HPP__
#define __REDIS_HPP__

#include<hiredis/hiredis.h>
#include<thread>
#include<functional>
#include<mutex>
#include<atomic>
#include<string>
#include<unordered_map>
using namespace std;

class redis{
private:
    // hiredis同步上下文对象，负责publish消息
    redisContext *publish_context;

    // hiredis同步上下文对象，负责subscribe消息
    redisContext *subscribe_context;

    // hiredis同步上下文对象，专用于普通命令（INCR取号等）
    // 【为什么要独立一条连接】
    // subscribe_context 处于订阅态且被 observer 线程独占 redisGetReply，
    // publish_context 会被多个IO线程并发使用。取号是强一致的读改写操作，
    // 必须走独立连接 + 互斥锁，否则会与其他命令抢占响应包，导致回包错位。
    redisContext *command_context;

    // 保护 command_context 的互斥锁（4个IO线程会并发取号）
    mutex _cmdMutex;

    // 保护 publish_context 的互斥锁
    mutex _pubMutex;

    // Redis 不可用时的本地降级发号器（保证服务不中断）
    // key: conversationId，value: 本地自增号
    unordered_map<string,uint64_t> _localSeq;
    mutex _localSeqMutex;
    atomic_bool _redisAvailable{false};

    // 回调操作,收到订阅的消息，给service层上报
    function<void(int, string)> _notify_message_handler;

public:
    redis();
    ~redis();

    //连接redis服务器
    bool connect();

    //向redis指定的通道channel发布消息
    bool publish(int channel, string message);

    //向redis指定的通道subscribe订阅消息
    bool subscribe(int channel);

    //向redis指定的通道unsubscribe取消订阅消息
    bool unsubscribe(int channel);

    //在独立线程中接收订阅通道中的消息
    void observer_channel_message();

    //初始化向业务层上报通道消息的回调对象
    void init_notify_handler(function<void(int, string)> fn);

    //====================== 会话级消息序号 ======================
    // 为指定会话原子分配下一个递增序号（从1开始）。
    //
    // 【为什么按会话分桶而不是全局一个序号】
    // 全局单调序号会让所有会话互相挤占号段，接收端无法判断"我这个会话
    // 是否缺消息"（因为空洞可能属于别的会话）。按会话取号后，同一会话内
    // 序号严格连续，接收端才能做去重与补洞判定。
    //
    // 【为什么用 Redis INCR 而不是本地自增】
    // 本项目是 Nginx 分发的多节点集群，同一会话的两条消息可能落在不同
    // 节点。本地自增会各自从1开始导致序号冲突。Redis INCR 是单线程原子
    // 操作，所有节点对同一个 key 取号，天然跨节点唯一且递增，且与消息
    // 落在哪个节点无关。计数本身由 Redis 的 RDB/AOF 保证持久性。
    //
    // 返回0表示取号彻底失败（调用方应视为异常）。
    uint64_t incrSeq(const string &conversationId);

    // 查询会话当前最大序号（不递增），用于客户端重连补齐判断
    uint64_t getSeq(const string &conversationId);

    bool redisAvailable()const{ return _redisAvailable.load(); }

private:
    // Redis 不可用时的降级取号：本地自增 + 高位标记，避免与Redis号段混淆
    uint64_t localFallbackSeq(const string &conversationId);
};

#endif
