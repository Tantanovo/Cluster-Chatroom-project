#ifndef CONVERSATION_HPP
#define CONVERSATION_HPP

#include<string>
#include<algorithm>
using namespace std;

// 会话ID规则（server与client共用，必须保持一致）
//
// 【为什么需要"会话"这个概念】
// 消息有序性只在会话内部才有意义：A和B的单聊消息需要严格有序，
// 但A与B的对话和C群里的消息之间没有可比的先后关系。
// 所以序号必须按会话分桶递增，接收端也只在会话内部排序。
namespace conv{

// 单聊会话ID：取两个userid的 min_max 形式。
//
// 【关键】必须与方向无关：A给B发 和 B给A发 属于同一个会话，
// 必须映射到同一个ID，否则两个方向各自维护一套序号，
// 双方看到的消息顺序就会不一致（Q5 的核心）。
inline string p2p(int userA,int userB){
    int lo=min(userA,userB);
    int hi=max(userA,userB);
    return "p" + to_string(lo) + "_" + to_string(hi);
}

// 群聊会话ID：群内所有成员共享同一个会话与同一套序号
inline string group(int groupid){
    return "g" + to_string(groupid);
}

}

#endif
