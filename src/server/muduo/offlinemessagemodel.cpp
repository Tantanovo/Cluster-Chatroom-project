#include"offlinemessagemodel.hpp"
#include"connectionpool.hpp"
#include<vector>
using namespace std;

//存储用户的离线消息
void OfflineMessageModel::insert(int userid,string msg){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return;

    const string sql="insert into OfflineMessage(userid,message) values(?,?)";
    conn->executeUpdate(sql,{to_string(userid),msg});
}

//删除用户的离线消息（登录拉取后清除）
void OfflineMessageModel::remove(int userid){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return;

    conn->executeUpdate("delete from OfflineMessage where userid=?",
                        {to_string(userid)});
}

//查询用户的离线消息
vector<string> OfflineMessageModel::query(int userid){
    vector<string> vec;
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return vec;

    const string sql="select message from OfflineMessage where userid=?";
    vector<vector<string>> rows;
    if(!conn->executeQuery(sql,{to_string(userid)},rows))return vec;

    for(const auto &row:rows)
        vec.push_back(row[0]);
    return vec;
}
