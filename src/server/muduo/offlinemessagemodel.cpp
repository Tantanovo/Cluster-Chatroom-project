#include"offlinemessagemodel.hpp"
#include"connectionpool.hpp"
#include"public.hpp"
#include"json.hpp"
#include<vector>
using json=nlohmann::json;
using namespace std;

//存储用户的离线消息
//【有序性】从消息体里抽出 convid/seq 单独存列，让数据库可以直接按
//(userid, convid, seq) 有序取出，而不是把排序全压在应用层。
void OfflineMessageModel::insert(int userid,string msg){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return;

    string convid="";
    uint64_t seq=0;
    try{
        json j=json::parse(msg);
        if(j.contains(MSG_FIELD_CONVID)&&j[MSG_FIELD_CONVID].is_string())
            convid=j[MSG_FIELD_CONVID].get<string>();
        if(j.contains(MSG_FIELD_SEQ)&&j[MSG_FIELD_SEQ].is_number_unsigned())
            seq=j[MSG_FIELD_SEQ].get<uint64_t>();
    }catch(const std::exception &){
        //非法JSON：仍然入库，convid/seq 留空，查询时排到末尾
    }

    const string sql=
        "insert into OfflineMessage(userid,convid,seq,message) values(?,?,?,?)";
    conn->executeUpdate(sql,{to_string(userid),convid,to_string(seq),msg});
}

//删除用户的离线消息（登录拉取后清除）
void OfflineMessageModel::remove(int userid){
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return;

    conn->executeUpdate("delete from OfflineMessage where userid=?",
                        {to_string(userid)});
}

//查询用户的离线消息
//直接在SQL里按 (convid, seq) 排序，命中 idx_user_conv_seq 索引，
//应用层的 sortOfflineMessages 只作为兜底（兼容无seq的历史数据）。
vector<string> OfflineMessageModel::query(int userid){
    vector<string> vec;
    auto conn=ConnectionPool::instance()->getConnection();
    if(conn==nullptr)return vec;

    const string sql=
        "select message from OfflineMessage where userid=? "
        "order by convid asc, seq asc, id asc";
    vector<vector<string>> rows;
    if(!conn->executeQuery(sql,{to_string(userid)},rows))return vec;

    for(const auto &row:rows)
        vec.push_back(row[0]);
    return vec;
}
