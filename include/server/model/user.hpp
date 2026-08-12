#ifndef USER_HPP
#define USER_HPP
#include <string>
using namespace std;
#include<iostream>
//匹配user表的ORM类
class User {
private:
    int id;
    string name;
    string password;
    string salt; // 密码哈希盐值（改造后新增，用于校验加盐哈希）
    string state; // online offline

public:
    User(int id=-1,string name="",string password="",string state="offline")
        : id(id), name(name), password(password), salt(""), state(state) {}
    void setId(int id) { this->id = id; }
    void setName(const string &name) { this->name = name; }
    void setPassword(const string &password) { this->password = password; }
    void setSalt(const string &salt) { this->salt = salt; }
    void setState(const string &state) { this->state = state; }

    int getId() const { return id; }
    string getName() const { return name; }
    string getPassword() const { return password; }
    string getSalt() const { return salt; }
    string getState() const { return state; }
};

#endif
