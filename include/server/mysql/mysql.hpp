#ifndef MYSQL_HPP
#define MYSQL_HPP
#include<muduo/base/Logging.h>
#include<mysql/mysql.h>
#include<string>
using namespace std;

// 数据库操作类
class MySQL{
private:
    MYSQL *_conn;
    // 数据库配置信息
    string server = "127.0.0.1";
    string user = "root";
    string password = "123456";
    string dbname = "chat";

public:
    MySQL();// 初始化数据库连接
    ~MySQL();// 释放数据库连接资源
    bool connect();// 连接数据库
    bool update(string sql);// 更新操作
    MYSQL_RES* query(string sql);// 查询操作
};
#endif