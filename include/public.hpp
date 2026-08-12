#ifndef PUBLIC_HPP
#define PUBLIC_HPP
//server和client公共的头文件
enum EnMsgType{
    LOGIN_MSG=1,//登录消息
    LOGIN_ACK_MSG,//登录响应消息
    LOGINOUT_MSG,//注销消息
    REG_MSG,//注册消息
    REG_ACK_MSG,//注册响应消息
    ONE_CHAT_MSG,//单人聊天消息
    ADD_FRIEND_MSG,//添加好友消息

    CREATE_GROUP_MSG,//创建群组消息
    ADD_GROUP_MSG,//加入群组消息
    GROUP_CHAT_MSG//群组聊天消息

};

// 聊天消息中与"有序性"相关的字段名（server与client共用，避免拼写漂移）
//
// convid : 会话ID，见 conversation.hpp
// seq    : 该会话内的单调递增序号，由服务端 Redis INCR 分配
// msgts  : 服务端接收时间戳(毫秒)，仅作展示与兜底排序，不用于判序
//
// 【为什么序号由服务端分配而不是客户端】
// 客户端时钟不可信（可被篡改、时钟漂移），且多端登录时无法协同。
// 服务端集中发号才能保证同一会话所有参与者看到完全一致的顺序。
#define MSG_FIELD_CONVID "convid"
#define MSG_FIELD_SEQ    "seq"
#define MSG_FIELD_TS     "msgts"

#endif
