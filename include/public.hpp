#ifndef PUBLIC_HPP
#define PUBLIC_HPP
//server和client公共的头文件
enum EnMsgType{
    LOGIN_MSG=1,//登录消息
    LOGIN_ACK_MSG,//登录响应消息
    REG_MSG,//注册消息
    REG_ACK_MSG,//注册响应消息
    ONE_CHAT_MSG,//单人聊天消息
    ADD_FRIEND_MSG,//添加好友消息

    CREATE_GROUP_MSG,//创建群组消息
    ADD_GROUP_MSG,//加入群组消息
    GROUP_CHAT_MSG//群组聊天消息
};

#endif