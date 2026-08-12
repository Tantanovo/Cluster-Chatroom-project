#include"crypto.hpp"
#include<openssl/sha.h>
#include<random>
#include<iomanip>
#include<sstream>
#include<cstring>

namespace crypto{

string makeSalt(){
    static const char *hex="0123456789abcdef";
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(0,15);
    string s;
    for(size_t i=0;i<32;++i)s+=hex[dis(gen)];
    return s;
}

string hashPassword(const string &salt,const string &pwd){
    string in=salt+pwd;
    unsigned char out[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(in.data()),in.size(),out);
    ostringstream oss;
    for(unsigned char c:out)
        oss<<hex<<setw(2)<<setfill('0')<<static_cast<int>(c);
    return oss.str();
}

bool safeEqual(const string &a,const string &b){
    if(a.size()!=b.size())return false;
    unsigned char diff=0;
    for(size_t i=0;i<a.size();++i)diff|=static_cast<unsigned char>(a[i]^b[i]);
    return diff==0;
}

}
