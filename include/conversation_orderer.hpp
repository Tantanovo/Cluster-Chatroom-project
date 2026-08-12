#ifndef CONVERSATION_ORDERER_HPP
#define CONVERSATION_ORDERER_HPP

#include<string>
#include<map>
#include<unordered_map>
#include<unordered_set>
#include<vector>
#include<chrono>
#include<functional>
#include"json.hpp"
using json=nlohmann::json;
using namespace std;

// 客户端会话内消息重排器
//
// ============================ 设计说明 ============================
// 解决的问题：服务端有三条投递路径（本节点直发 / Redis跨节点转发 /
// 离线补投），网络延迟不同会导致消息乱序到达。序号由服务端按会话统一
// 分配，客户端据此在"会话内部"恢复正确顺序。
//
// 为什么只按会话排序、不做全局排序：
//   不同会话之间没有可比较的先后关系（A的私聊和某群消息谁先谁后无意义），
//   全局排序反而会因为某个会话缺号而卡住其他会话的消息展示。
//
// 三种情况的处理（这是"每次来消息要不要查序号"的真正答案：
//   不是每条都去查服务端，而是客户端本地维护 expectSeq 做判定）：
//   1) seq == expect  : 连续，立即上屏，expect++，并尝试排空缓存区
//   2) seq >  expect  : 中间有洞（消息还在路上或丢了），先缓存不上屏
//   3) seq <  expect  : 重复消息（重连补投/重传），直接丢弃
//
// 空洞不能无限等待：Redis Pub/Sub 本身不保证送达，缺的消息可能永远不来。
// 因此设置等待超时（默认800ms），超时后放弃等待、按序号升序强制放行，
// 保证可用性优先（聊天场景下"迟到但有序"优于"永久卡住"）。
class ConversationOrderer{
public:
    // 上屏回调：由调用方决定如何渲染
    using Emit=function<void(const json &)>;

    explicit ConversationOrderer(Emit emit,int gapTimeoutMs=800)
        :_emit(std::move(emit)),_gapTimeoutMs(gapTimeoutMs){}

    // 收到一条消息（必须含 convid/seq；缺字段的旧消息直接放行以兼容）
    void onMessage(const json &js){
        if(!js.contains("convid")||!js.contains("seq")||
           !js["seq"].is_number_unsigned()){
            //兼容不带序号的消息（如系统通知、改造前的历史离线消息）
            _emit(js);
            return;
        }
        const string conv=js["convid"].get<string>();
        const uint64_t seq=js["seq"].get<uint64_t>();
        Channel &ch=_channels[conv];

        //首次见到该会话：以当前消息作为起点，避免误判前面全是空洞
        if(!ch.initialized){
            ch.initialized=true;
            ch.expect=seq;
        }

        //情况3：重复或过期消息 —— 去重丢弃
        if(seq<ch.expect||ch.delivered.count(seq)>0){
            return;
        }

        //情况1：正好是期望的下一条
        if(seq==ch.expect){
            deliver(ch,js,seq);
            drainPending(ch);//看缓存里是否已经攒够后续连续消息
            return;
        }

        //情况2：seq>expect，出现空洞，先缓存
        ch.pending[seq]=js;
        if(ch.gapSince==Clock::time_point{}){
            ch.gapSince=Clock::now();//记录空洞开始时间，用于超时放行
        }
    }

    // 需要被周期性调用（或在每次收发消息前调用）：
    // 检查是否有空洞已等待超时，超时则强制放行，避免消息永久卡在缓存里。
    void tick(){
        const auto now=Clock::now();
        for(auto &kv:_channels){
            Channel &ch=kv.second;
            if(ch.pending.empty())continue;
            if(ch.gapSince==Clock::time_point{})continue;
            const auto waited=chrono::duration_cast<chrono::milliseconds>(
                now-ch.gapSince).count();
            if(waited<_gapTimeoutMs)continue;

            //超时：放弃等待缺失的消息，把缓存里最小序号当作新的期望值
            const uint64_t firstPending=ch.pending.begin()->first;
            ch.expect=firstPending;
            drainPending(ch);
        }
    }

    // 批量处理离线消息（登录时一次性补投）：
    // 服务端已按 (convid,seq) 排好序，这里仍走同一条通道保证去重生效。
    void onOfflineBatch(const vector<json> &msgs){
        for(const auto &m:msgs)onMessage(m);
        //离线批次结束后立即排空，不必等超时
        for(auto &kv:_channels){
            Channel &ch=kv.second;
            if(!ch.pending.empty()){
                ch.expect=ch.pending.begin()->first;
                drainPending(ch);
            }
        }
    }

private:
    using Clock=chrono::steady_clock;

    struct Channel{
        bool initialized=false;
        uint64_t expect=0;                  //期望的下一个序号
        map<uint64_t,json> pending;         //乱序到达的缓存（按序号自动排序）
        unordered_set<uint64_t> delivered;  //已上屏序号，用于去重
        Clock::time_point gapSince{};       //空洞开始等待的时刻
    };

    void deliver(Channel &ch,const json &js,uint64_t seq){
        _emit(js);
        ch.delivered.insert(seq);
        ch.expect=seq+1;
        ch.gapSince=Clock::time_point{};//空洞已填补，清空计时
        //控制去重集合大小：只需记住最近的序号，早于expect的靠比较即可判重
        if(ch.delivered.size()>4096){
            ch.delivered.clear();
        }
    }

    //把缓存中与当前expect连续的消息依次上屏
    void drainPending(Channel &ch){
        while(!ch.pending.empty()){
            auto it=ch.pending.begin();
            if(it->first<ch.expect){
                ch.pending.erase(it);//已过期，丢弃
                continue;
            }
            if(it->first!=ch.expect)break;//仍有空洞，停止
            json m=it->second;
            ch.pending.erase(it);
            deliver(ch,m,ch.expect);
        }
        //缓存里还有内容说明空洞仍在，重置计时起点
        if(!ch.pending.empty()&&ch.gapSince==Clock::time_point{}){
            ch.gapSince=Clock::now();
        }
    }

    Emit _emit;
    int _gapTimeoutMs;
    unordered_map<string,Channel> _channels;
};

#endif
