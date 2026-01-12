#include"mysql.hpp"

MySQL::MySQL(){// 初始化数据库连接
        _conn = mysql_init(nullptr);
}
MySQL::~MySQL(){// 释放数据库连接资源
    if (_conn != nullptr)mysql_close(_conn);
}
bool MySQL::connect(){// 连接数据库
    MYSQL *p = mysql_real_connect(_conn, server.c_str(), user.c_str(),password.c_str(), dbname.c_str(), 3306, nullptr, 0);
    if (p != nullptr){
        //c和c++默认的编码格式是ascii，而中文使用gbk编码格式
       mysql_query(_conn, "set names gbk");
    }
    return p != nullptr;
}
bool MySQL::update(string sql){// 更新操作
    if (mysql_query(_conn, sql.c_str())){
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "更新失败!";
        return false;
    }
    return true;
}
MYSQL_RES* MySQL::query(string sql){// 查询操作
    if (mysql_query(_conn, sql.c_str())){
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "查询失败!";
        return nullptr;
    }
    return mysql_use_result(_conn);
}