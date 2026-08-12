#include"usermodel.hpp"
#include"connectionpool.hpp"
#include"crypto.hpp"
#include<muduo/base/Logging.h>
#include<vector>
using namespace std;

//user表的增加方法
//改造前：sprintf拼SQL + 密码明文入库 + 每次新建连接
//改造后：预编译语句 + 加盐哈希存储 + 连接池复用
bool UserModel::insert(User &user){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return false;

    string salt=crypto::makeSalt();
    string hashed=crypto::hashPassword(salt,user.getPassword());

    const string sql=
        "insert into User(name,password,salt,state) values(?,?,?,?)";
    if(!conn->executeUpdate(sql,{user.getName(),hashed,salt,user.getState()}))
        return false;

    user.setId(static_cast<int>(conn->lastInsertId()));
    return true;
}

User UserModel::query(int id){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return User();

    const string sql="select id,name,password,salt,state from User where id=?";
    vector<vector<string>> rows;
    if(!conn->executeQuery(sql,{to_string(id)},rows)||rows.empty())
        return User();

    User user;
    user.setId(stoi(rows[0][0]));
    user.setName(rows[0][1]);
    user.setPassword(rows[0][2]);
    user.setSalt(rows[0][3]);
    user.setState(rows[0][4]);
    return user;
}

User UserModel::queryByName(string name){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return User();

    //改造前：sprintf(sql,"select * from User where name='%s'",name.c_str())
    //        —— 用户名传 " or "1"="1 即可绕过登录，预编译后注入不再可能
    const string sql="select id,name,password,salt,state from User where name=?";
    vector<vector<string>> rows;
    if(!conn->executeQuery(sql,{name},rows)||rows.empty())
        return User();

    User user;
    user.setId(stoi(rows[0][0]));
    user.setName(rows[0][1]);
    user.setPassword(rows[0][2]);
    user.setSalt(rows[0][3]);
    user.setState(rows[0][4]);
    return user;
}

bool UserModel::updatestate(User &user){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return false;

    const string sql="update User set state=? where id=?";
    return conn->executeUpdate(sql,{user.getState(),to_string(user.getId())});
}

//服务器退出时把所有online用户重置为offline，避免僵尸在线状态
void UserModel::resetState(){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return;
    conn->executeUpdate("update User set state='offline' where state='online'",{});
}
