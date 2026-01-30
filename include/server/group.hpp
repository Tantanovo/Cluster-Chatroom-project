#ifndef GROUP_HPP
#define GROUP_HPP
using namespace std;
#include"groupuser.hpp"
#include<string>
#include<vector>
//User表的数据操作类ORM类
class Group{
private:
    int id;
    string name;
    string desc;
    vector<Groupuser> users; //群成员列表
public:
    Group(int id=-1,string name="",string desc=""):id(id),name(name),desc(desc){}

    void setId(int id){this->id=id;}
    void setName(const string &name){this->name=name;}
    void setDesc(const string &desc){this->desc=desc;}
    int getId()const{return id;}
    string getName()const{return name;}
    string getDesc()const{return desc;}

    vector<Groupuser>& getUsers(){return users;}
};
#endif