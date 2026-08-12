#include"connectionpool.hpp"
#include<muduo/base/Logging.h>
#include<thread>
#include<fstream>
#include<sstream>
#include<cstdlib>
#include<chrono>

ConnectionPool *ConnectionPool::instance(){
    static ConnectionPool pool;//C++11起局部静态初始化线程安全
    return &pool;
}

bool ConnectionPool::loadConfig(){
    //优先环境变量，其次配置文件。绝不把密码写进源码提交到GitHub。
    if(const char *h=getenv("CHAT_DB_HOST"))_host=h;
    if(const char *u=getenv("CHAT_DB_USER"))_user=u;
    if(const char *p=getenv("CHAT_DB_PASSWD"))_passwd=p;
    if(const char *d=getenv("CHAT_DB_NAME"))_dbname=d;

    ifstream ifs("conf/mysql.cnf");
    if(ifs){
        string line;
        while(getline(ifs,line)){
            if(line.empty()||line[0]=='#')continue;
            auto pos=line.find('=');
            if(pos==string::npos)continue;
            string k=line.substr(0,pos),v=line.substr(pos+1);
            if(k=="host")_host=v;
            else if(k=="user")_user=v;
            else if(k=="password")_passwd=v;
            else if(k=="dbname")_dbname=v;
            else if(k=="port")_port=static_cast<uint16_t>(stoi(v));
            else if(k=="initSize")_initSize=stoi(v);
            else if(k=="maxSize")_maxSize=stoi(v);
            else if(k=="maxIdleTime")_maxIdleTime=stoi(v);
        }
    }
    if(_host.empty())_host="127.0.0.1";
    if(_dbname.empty())_dbname="chat";
    return !_user.empty();
}

ConnectionPool::ConnectionPool(){
    if(!loadConfig()){
        LOG_FATAL<<"db config missing: set CHAT_DB_USER or conf/mysql.cnf";
    }

    //预创建initSize条连接，把手握开销从请求路径挪到启动阶段
    for(int i=0;i<_initSize;++i){
        Connection *c=new Connection();
        if(!c->connect(_host,_port,_user,_passwd,_dbname)){
            delete c;
            LOG_FATAL<<"init connection failed, host="<<_host<<" user="<<_user;
        }
        c->refreshAliveTime();
        _connQueue.push(c);
        ++_connCount;
    }

    thread producer(&ConnectionPool::produceConnection,this);
    producer.detach();
    thread scanner(&ConnectionPool::scanIdleConnection,this);
    scanner.detach();

    LOG_INFO<<"connection pool ready, initSize="<<_initSize
            <<" maxSize="<<_maxSize;
}

ConnectionPool::~ConnectionPool(){
    lock_guard<mutex> lk(_queueMutex);
    while(!_connQueue.empty()){
        delete _connQueue.front();
        _connQueue.pop();
    }
}

//生产者：队列空时被唤醒，若未达上限则新建一条
void ConnectionPool::produceConnection(){
    for(;;){
        unique_lock<mutex> lk(_queueMutex);
        _cv.wait(lk,[this]{return _connQueue.empty()||_connCount>=_maxSize;});

        if(_connCount<_maxSize){
            Connection *c=new Connection();
            if(c->connect(_host,_port,_user,_passwd,_dbname)){
                c->refreshAliveTime();
                _connQueue.push(c);
                ++_connCount;
            }else{
                delete c;
            }
        }
        _cv.notify_all();//唤醒等待取连接的消费者
    }
}

shared_ptr<Connection> ConnectionPool::getConnection(){
    unique_lock<mutex> lk(_queueMutex);

    while(_connQueue.empty()){
        //用wait_for而不是wait：池被打满时线程不会永久挂死，而是超时返回
        if(_cv.wait_for(lk,chrono::milliseconds(_connectTimeout))
                ==cv_status::timeout){
            if(_connQueue.empty()){
                LOG_ERROR<<"get connection timeout, pool exhausted";
                return nullptr;
            }
        }
    }

    //自定义删除器：shared_ptr析构时把连接放回队列，而不是delete
    shared_ptr<Connection> sp(_connQueue.front(),[this](Connection *c){
        lock_guard<mutex> lk2(_queueMutex);
        c->refreshAliveTime();//归还时刷新空闲起点
        _connQueue.push(c);
        _cv.notify_all();
    });
    _connQueue.pop();

    //长时间空闲的连接可能已被MySQL服务端断开，先探活
    if(!sp->ping()){
        LOG_WARN<<"stale connection detected, reconnecting";
        sp->connect(_host,_port,_user,_passwd,_dbname);
    }

    _cv.notify_all();//通知生产者检查是否需要补充
    return sp;
}

//回收线程：定期清理空闲过久的"超出initSize的"连接，把连接数还回去
void ConnectionPool::scanIdleConnection(){
    for(;;){
        this_thread::sleep_for(chrono::seconds(_maxIdleTime));
        lock_guard<mutex> lk(_queueMutex);
        while(_connCount>_initSize&&!_connQueue.empty()){
            Connection *c=_connQueue.front();
            if(c->idleTime()/CLOCKS_PER_SEC>=static_cast<clock_t>(_maxIdleTime)){
                _connQueue.pop();
                delete c;
                --_connCount;
            }else{
                break;//队首都没超时，后面的更新，直接退出
            }
        }
    }
}
