#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include<string>
using namespace std;

//密码哈希工具：SHA-256(salt + password)
//改造前密码明文入库，改造后存 hash + salt，明文密码不再落盘。
namespace crypto{

//生成随机salt（16字节十六进制，32字符）
string makeSalt();

//SHA-256(salt + password) 十六进制输出（64字符）
string hashPassword(const string &salt,const string &pwd);

//常量时间比较，防时序侧信道（简单实现，足够本项目使用）
bool safeEqual(const string &a,const string &b);

}

#endif
