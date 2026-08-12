#include"groupmodel.hpp"
#include"connectionpool.hpp"
#include<muduo/base/Logging.h>
#include<vector>
using namespace std;

//创建群组（单条insert）
bool GroupModel::createGroup(Group &group){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return false;

    const string sql="insert into AllGroup(groupname,groupdesc) values(?,?)";
    if(!conn->executeUpdate(sql,{group.getName(),group.getDesc()}))
        return false;
    group.setId(static_cast<int>(conn->lastInsertId()));
    return true;
}

//创建群组 + 创建者成员关系：同一个连接、同一个事务
//改造前：两条独立语句各自提交，第二条失败会留下孤儿群组
bool GroupModel::createGroupWithCreator(Group &group,int creatorId){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return false;

    if(!conn->begin())return false;

    bool ok=conn->executeUpdate(
        "insert into AllGroup(groupname,groupdesc) values(?,?)",
        {group.getName(),group.getDesc()});
    if(ok){
        group.setId(static_cast<int>(conn->lastInsertId()));
        ok=conn->executeUpdate(
            "insert into GroupUser(groupid,userid,role) values(?,?,?)",
            {to_string(group.getId()),to_string(creatorId),"creator"});
    }

    if(ok){
        conn->commit();
    }else{
        conn->rollback();
        LOG_ERROR<<"createGroup rolled back, creator="<<creatorId;
    }
    return ok;
}

//加入群组
void GroupModel::addGroup(int userid,int groupid,string role){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return;

    const string sql="insert into GroupUser(groupid,userid,role) values(?,?,?)";
    conn->executeUpdate(sql,{to_string(groupid),to_string(userid),role});
}

//查询用户所在的群组信息
vector<Group> GroupModel::queryGroups(int userid){
    vector<Group> vec;
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return vec;

    //1.根据userid查出所属群组
    const string sqlGroups=
        "select a.id,a.groupname,a.groupdesc "
        "from AllGroup a inner join GroupUser b on a.id=b.groupid "
        "where b.userid=?";
    vector<vector<string>> rows;
    if(!conn->executeQuery(sqlGroups,{to_string(userid)},rows))return vec;

    for(const auto &row:rows){
        Group group;
        group.setId(stoi(row[0]));
        group.setName(row[1]);
        group.setDesc(row[2]);
        vec.push_back(group);
    }

    //2.再查每个群的所有成员
    const string sqlUsers=
        "select a.id,a.name,a.state,b.role "
        "from User a inner join GroupUser b on a.id=b.userid "
        "where b.groupid=?";
    for(Group &group:vec){
        vector<vector<string>> memberRows;
        if(!conn->executeQuery(sqlUsers,{to_string(group.getId())},memberRows))
            continue;
        for(const auto &r:memberRows){
            Groupuser user;
            user.setId(stoi(r[0]));
            user.setName(r[1]);
            user.setState(r[2]);
            user.setRole(r[3]);
            group.getUsers().push_back(user);
        }
    }
    return vec;
}

//查询群组成员id列表，除userid自己，用于群聊给其他成员群发
vector<int> GroupModel::queryGroupUsers(int userid,int groupid){
    vector<int> vec;
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return vec;

    const string sql=
        "select userid from GroupUser where groupid=? and userid!=?";
    vector<vector<string>> rows;
    if(!conn->executeQuery(sql,{to_string(groupid),to_string(userid)},rows))
        return vec;

    for(const auto &row:rows)
        vec.push_back(stoi(row[0]));
    return vec;
}
