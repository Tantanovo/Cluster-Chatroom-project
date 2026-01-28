#ifndef PUBLIC_HPP
#define PUBLIC_HPP
//server和client公共的头文件
enum EnMsgType{
    LOGIN_MSG=1,//登录消息
    LOGIN_ACK_MSG,//登录响应消息
    REG_MSG,//注册消息
    REG_ACK_MSG,//注册响应消息
};

#endif