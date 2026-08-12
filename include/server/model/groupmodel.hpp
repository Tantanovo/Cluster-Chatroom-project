#ifndef GROUPMODEL_HPP
#define GROUPMODEL_HPP
#include"group.hpp"
#include<vector>
#include<string>
using namespace std;
//维护群组信息的操作接口方法
class GroupModel{
public:
    //创建群组（单条insert，事务由外层组合）
    bool createGroup(Group &group);
    //创建群组 + 创建者成员关系，两条insert在同一事务中，失败整体回滚
    //（改造前是两条独立语句，第二条失败会留下"无创建者、无成员"的孤儿群组）
    bool createGroupWithCreator(Group &group,int creatorId);
    //加入群组
    void addGroup(int userid,int groupid,string role);
    //查询用户所在的群组信息
    vector<Group> queryGroups(int userid);
    //根据指定的群组id查询群组用户id列表，除userid自己，主要用于群聊业务给其他成员群发消息
    vector<int> queryGroupUsers(int userid,int groupid);
};

#endif
