#ifndef GROUPUSER_HPP
#define GROUPUSER_HPP
#include"user.hpp"
//群组用户，多了一个role角色信息，从user类直接继承，复用user的其他信息
class Groupuser:public User{
private:
    string role; 
public:
    void setRole(const string &role){this->role=role;}
    string getRole()const{return role;}
};
#endif