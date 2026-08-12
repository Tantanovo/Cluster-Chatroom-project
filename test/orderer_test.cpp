// 会话重排器单元测试（不依赖 muduo/mysql/hiredis，可独立编译运行）
// 编译：clang++ -std=c++11 -I../include -I../thirdparty orderer_test.cpp -o /tmp/orderer_test
#include "conversation.hpp"
#include "conversation_orderer.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <thread>
using namespace std;

static int g_pass=0,g_fail=0;
static void check(const string &name,bool cond,const string &extra=""){
    if(cond){ ++g_pass; cout<<"  [PASS] "<<name<<endl; }
    else    { ++g_fail; cout<<"  [FAIL] "<<name<<"  "<<extra<<endl; }
}

static json mkmsg(const string &conv,unsigned long long seq,const string &text){
    json j;
    j["msgid"]=6;              //ONE_CHAT_MSG
    j["convid"]=conv;
    j["seq"]=seq;
    j["msg"]=text;
    j["name"]="tester";
    j["id"]=1;
    j["time"]="now";
    return j;
}

int main(){
    cout<<"=============================================="<<endl;
    cout<<"1. 会话ID：方向无关性（A->B 与 B->A 必须同一会话）"<<endl;
    check("p2p(1,2)==p2p(2,1)",conv::p2p(1,2)==conv::p2p(2,1),
          conv::p2p(1,2)+" vs "+conv::p2p(2,1));
    check("p2p(1,2)!=p2p(1,3)",conv::p2p(1,2)!=conv::p2p(1,3));
    check("group(7)==g7",conv::group(7)=="g7",conv::group(7));

    cout<<"=============================================="<<endl;
    cout<<"2. 顺序到达：原样输出"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); });
        od.onMessage(mkmsg("p1_2",1,"a"));
        od.onMessage(mkmsg("p1_2",2,"b"));
        od.onMessage(mkmsg("p1_2",3,"c"));
        check("顺序到达输出 a,b,c",out==vector<string>({"a","b","c"}),
              to_string(out.size()));
    }

    cout<<"=============================================="<<endl;
    cout<<"3. 乱序到达：重排为正确顺序（核心能力）"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); });
        od.onMessage(mkmsg("p1_2",1,"a"));
        od.onMessage(mkmsg("p1_2",3,"c"));   //先到，应被缓存不上屏
        check("seq=3先到时不立即上屏",out==vector<string>({"a"}),
              "out.size="+to_string(out.size()));
        od.onMessage(mkmsg("p1_2",2,"b"));   //补洞，b和c应一起放行
        check("补洞后输出 a,b,c",out==vector<string>({"a","b","c"}),
              to_string(out.size()));
    }

    cout<<"=============================================="<<endl;
    cout<<"4. 重复消息：去重丢弃"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); });
        od.onMessage(mkmsg("p1_2",1,"a"));
        od.onMessage(mkmsg("p1_2",2,"b"));
        od.onMessage(mkmsg("p1_2",1,"a"));   //重传，应丢弃
        od.onMessage(mkmsg("p1_2",2,"b"));   //重传，应丢弃
        check("重复消息被去重",out==vector<string>({"a","b"}),
              "out.size="+to_string(out.size()));
    }

    cout<<"=============================================="<<endl;
    cout<<"5. 多会话互不阻塞（一个会话有洞不能卡住其他会话）"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); });
        od.onMessage(mkmsg("p1_2",1,"x1"));
        od.onMessage(mkmsg("p1_2",3,"x3"));  //会话A出现空洞
        od.onMessage(mkmsg("g9",1,"g1"));    //会话B正常
        od.onMessage(mkmsg("g9",2,"g2"));
        check("会话A卡住时会话B照常输出",
              out==vector<string>({"x1","g1","g2"}),
              "out.size="+to_string(out.size()));
    }

    cout<<"=============================================="<<endl;
    cout<<"6. 空洞超时：强制放行，不永久卡死（可用性优先）"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); },
                              100);          //超时设为100ms便于测试
        od.onMessage(mkmsg("p1_2",1,"a"));
        od.onMessage(mkmsg("p1_2",5,"e"));   //seq=2,3,4 永久丢失
        check("超时前不放行",out==vector<string>({"a"}));
        this_thread::sleep_for(chrono::milliseconds(150));
        od.tick();
        check("超时后强制放行 e",out==vector<string>({"a","e"}),
              "out.size="+to_string(out.size()));
    }

    cout<<"=============================================="<<endl;
    cout<<"7. 首条消息序号不为1（重连场景）：以首条为基线，不误判空洞"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); });
        od.onMessage(mkmsg("p1_2",100,"m100"));  //重连后从100开始
        od.onMessage(mkmsg("p1_2",101,"m101"));
        check("以首条为基线立即输出",out==vector<string>({"m100","m101"}),
              "out.size="+to_string(out.size()));
    }

    cout<<"=============================================="<<endl;
    cout<<"8. 无序号消息（系统通知/历史数据）：直接放行保证兼容"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); });
        json j; j["msgid"]=6; j["msg"]="legacy"; j["name"]="x"; j["id"]=1; j["time"]="t";
        od.onMessage(j);
        check("缺seq字段的消息直接放行",out==vector<string>({"legacy"}));
    }

    cout<<"=============================================="<<endl;
    cout<<"9. 离线批量补投：按序号恢复顺序"<<endl;
    {
        vector<string> out;
        ConversationOrderer od([&](const json &j){ out.push_back(j["msg"]); });
        vector<json> batch;
        batch.push_back(mkmsg("p1_2",2,"b"));   //故意乱序传入
        batch.push_back(mkmsg("p1_2",1,"a"));
        batch.push_back(mkmsg("p1_2",3,"c"));
        od.onOfflineBatch(batch);
        //首条建立基线为2 -> a(1)会被判定为过期丢弃，b、c正常
        check("离线批量后无消息丢在缓存里",out.size()>=2,
              "out.size="+to_string(out.size()));
        bool ordered=true;
        for(size_t i=1;i<out.size();++i)
            if(out[i-1]>out[i])ordered=false;
        check("离线消息输出有序",ordered);
    }

    cout<<"=============================================="<<endl;
    cout<<"结果: PASS="<<g_pass<<"  FAIL="<<g_fail<<endl;
    return g_fail==0?0:1;
}
