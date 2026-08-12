#include"connection.hpp"
#include<muduo/base/Logging.h>
#include<cstring>

Connection::Connection(){
    _conn=mysql_init(nullptr);
    //断线自动重连，配合ping使用
    bool reconnect=true;
    mysql_options(_conn,MYSQL_OPT_RECONNECT,&reconnect);
}

Connection::~Connection(){
    if(_conn!=nullptr)mysql_close(_conn);
}

bool Connection::connect(const string &host,uint16_t port,
                         const string &user,const string &passwd,const string &dbname){
    MYSQL *p=mysql_real_connect(_conn,host.c_str(),user.c_str(),
                                passwd.c_str(),dbname.c_str(),port,nullptr,0);
    if(p!=nullptr){
        //【修正】原代码写的是"set names gbk"，会导致UTF-8客户端发来的
        //中文消息乱码。统一使用utf8mb4。
        mysql_query(_conn,"set names utf8mb4");
        refreshAliveTime();
        return true;
    }
    LOG_ERROR<<"mysql connect failed: "<<mysql_error(_conn);
    return false;
}

bool Connection::ping(){
    return _conn!=nullptr && mysql_ping(_conn)==0;
}

uint64_t Connection::lastInsertId(){
    return mysql_insert_id(_conn);
}

bool Connection::begin(){   return mysql_query(_conn,"start transaction")==0; }
bool Connection::commit(){  return mysql_query(_conn,"commit")==0; }
bool Connection::rollback(){return mysql_query(_conn,"rollback")==0; }

//预编译语句：SQL结构与数据分离，参数永远被当作值而非语法
bool Connection::executeUpdate(const string &sql,const vector<string> &params){
    MYSQL_STMT *stmt=mysql_stmt_init(_conn);
    if(stmt==nullptr)return false;

    if(mysql_stmt_prepare(stmt,sql.c_str(),sql.size())!=0){
        LOG_ERROR<<"stmt_prepare failed: "<<mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    vector<MYSQL_BIND> binds(params.size());
    vector<unsigned long> lens(params.size());
    if(!params.empty()){
        ::memset(binds.data(),0,sizeof(MYSQL_BIND)*params.size());
        for(size_t i=0;i<params.size();++i){
            lens[i]=params[i].size();
            binds[i].buffer_type=MYSQL_TYPE_STRING;
            binds[i].buffer=const_cast<char*>(params[i].c_str());
            binds[i].buffer_length=lens[i];
            binds[i].length=&lens[i];
        }
        if(mysql_stmt_bind_param(stmt,binds.data())!=0){
            mysql_stmt_close(stmt);
            return false;
        }
    }

    bool ok=(mysql_stmt_execute(stmt)==0);
    if(!ok)LOG_ERROR<<"stmt_execute failed: "<<mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    refreshAliveTime();
    return ok;
}

bool Connection::executeQuery(const string &sql,const vector<string> &params,
                              vector<vector<string>> &out){
    out.clear();
    MYSQL_STMT *stmt=mysql_stmt_init(_conn);
    if(stmt==nullptr)return false;

    if(mysql_stmt_prepare(stmt,sql.c_str(),sql.size())!=0){
        mysql_stmt_close(stmt);
        return false;
    }

    vector<MYSQL_BIND> inBinds(params.size());
    vector<unsigned long> inLens(params.size());
    if(!params.empty()){
        ::memset(inBinds.data(),0,sizeof(MYSQL_BIND)*params.size());
        for(size_t i=0;i<params.size();++i){
            inLens[i]=params[i].size();
            inBinds[i].buffer_type=MYSQL_TYPE_STRING;
            inBinds[i].buffer=const_cast<char*>(params[i].c_str());
            inBinds[i].buffer_length=inLens[i];
            inBinds[i].length=&inLens[i];
        }
        mysql_stmt_bind_param(stmt,inBinds.data());
    }

    if(mysql_stmt_execute(stmt)!=0){
        LOG_ERROR<<"query execute failed: "<<mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    //把结果集全部拉到客户端，避免use_result占着连接不放
    if(mysql_stmt_store_result(stmt)!=0){
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_RES *meta=mysql_stmt_result_metadata(stmt);
    if(meta==nullptr){ mysql_stmt_close(stmt); return true; }//无结果集的语句
    const unsigned int cols=mysql_num_fields(meta);

    const size_t kBuf=4096;
    vector<vector<char>> bufs(cols,vector<char>(kBuf));
    vector<unsigned long> outLens(cols,0);
    vector<my_bool> isNulls(cols,0);
    vector<MYSQL_BIND> outBinds(cols);
    ::memset(outBinds.data(),0,sizeof(MYSQL_BIND)*cols);
    for(unsigned int i=0;i<cols;++i){
        outBinds[i].buffer_type=MYSQL_TYPE_STRING;
        outBinds[i].buffer=bufs[i].data();
        outBinds[i].buffer_length=kBuf;
        outBinds[i].length=&outLens[i];
        outBinds[i].is_null=&isNulls[i];
    }
    mysql_stmt_bind_result(stmt,outBinds.data());

    while(mysql_stmt_fetch(stmt)==0){
        vector<string> row;
        row.reserve(cols);
        for(unsigned int i=0;i<cols;++i){
            row.emplace_back(isNulls[i]?string():string(bufs[i].data(),outLens[i]));
        }
        out.push_back(std::move(row));
    }

    mysql_free_result(meta);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
    refreshAliveTime();
    return true;
}
