#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include<mysql/mysql.h>
#include<string>
#include<vector>
#include<ctime>
using namespace std;

//单条MySQL连接的封装
class Connection{
public:
    Connection();
    ~Connection();

    bool connect(const string &host,uint16_t port,
                 const string &user,const string &passwd,const string &dbname);

    //---- 预编译语句接口：彻底消除SQL注入 ----
    //写操作，params依次绑定sql里的?占位符
    bool executeUpdate(const string &sql,const vector<string> &params);
    //读操作，返回二维结果集
    bool executeQuery(const string &sql,const vector<string> &params,
                      vector<vector<string>> &out);

    //最后一次insert产生的自增主键
    uint64_t lastInsertId();

    //---- 事务控制 ----
    bool begin();
    bool commit();
    bool rollback();

    //空闲计时，供连接池回收判断
    void refreshAliveTime(){ _aliveTime=::clock(); }
    clock_t idleTime()const{ return ::clock()-_aliveTime; }

    //长空闲后探活（MySQL服务端wait_timeout默认8小时会踢掉空闲连接）
    bool ping();

private:
    MYSQL *_conn=nullptr;
    clock_t _aliveTime=0;
};

#endif
