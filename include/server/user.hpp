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
    string state; // online offline

public:
    User(int id=-1,string name="",string password="",string state="offline") : id(id), name(name), password(password), state(state) {}
    void setId(int id) { this->id = id; }
    void setName(const string &name) { this->name = name; }
    void setPassword(const string &password) { this->password = password; }
    void setState(const string &state) { this->state = state; }

    int getId() const { return id; }
    string getName() const { return name; }
    string getPassword() const { return password; }
    string getState() const { return state; }
};

#endif