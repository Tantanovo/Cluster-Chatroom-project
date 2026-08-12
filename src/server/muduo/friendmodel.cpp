#include"friendmodel.hpp"
#include"connectionpool.hpp"
#include<vector>
using namespace std;

//添加好友关系
void FriendModel::insert(int userid,int friendid){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return;

    const string sql="insert into Friend(userid,friendid) values(?,?)";
    conn->executeUpdate(sql,{to_string(userid),to_string(friendid)});
}

//返回用户的好友列表
vector<User> FriendModel::query(int userid){
    vector<User> vec;
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return vec;

    //改造前：sprintf拼多表SQL；改造后：预编译 + 连接池
    const string sql=
        "select a.id,a.name,a.password,a.salt,a.state "
        "from User a inner join Friend b on a.id=b.friendid "
        "where b.userid=?";
    vector<vector<string>> rows;
    if(!conn->executeQuery(sql,{to_string(userid)},rows))return vec;

    for(const auto &row:rows){
        User user;
        user.setId(stoi(row[0]));
        user.setName(row[1]);
        user.setPassword(row[2]);
        user.setSalt(row[3]);
        user.setState(row[4]);
        vec.push_back(user);
    }
    return vec;
}
