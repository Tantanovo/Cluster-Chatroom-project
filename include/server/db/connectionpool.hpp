#ifndef CONNECTIONPOOL_HPP
#define CONNECTIONPOOL_HPP

#include"connection.hpp"
#include<queue>
#include<mutex>
#include<atomic>
#include<memory>
#include<condition_variable>
using namespace std;

//生产者消费者模型的MySQL连接池（单例）
//改造前：每次查询都新建一条MySQL连接（一次登录要建4~6条又立刻销毁），
//        TCP三次握手+认证握手开销全部压在请求路径上，这是最大的性能瓶颈。
//改造后：启动时预建initSize条连接，getConnection返回带自定义删除器的
//        shared_ptr，出作用域自动"归还"连接而非关闭。
class ConnectionPool{
public:
    static ConnectionPool *instance();

    //取连接：池空时阻塞等待_connectTimeout毫秒，超时返回nullptr
    //（用wait_for而不是wait，池被打满时线程不会永久挂死）
    shared_ptr<Connection> getConnection();

private:
    ConnectionPool();
    ~ConnectionPool();
    ConnectionPool(const ConnectionPool &)=delete;
    ConnectionPool &operator=(const ConnectionPool &)=delete;

    bool loadConfig();          //从环境变量/配置文件读，凭据不再硬编码进源码
    void produceConnection();   //生产者线程：队列空时按需新建，不超过_maxSize
    void scanIdleConnection();  //回收线程：清理超过_maxIdleTime的多余空闲连接

    string _host,_user,_passwd,_dbname;
    uint16_t _port=3306;

    int _initSize=10;       //初始连接数
    int _maxSize=64;        //连接上限
    int _maxIdleTime=60;    //最大空闲秒数，超过则回收（保留initSize条）
    int _connectTimeout=100;//获取连接的超时毫秒数

    queue<Connection*> _connQueue;
    mutex _queueMutex;
    condition_variable _cv;
    atomic_int _connCount{0};//已创建连接总数
};

#endif
