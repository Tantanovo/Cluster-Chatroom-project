-- 集群聊天室数据库初始化脚本
-- 用法：mysql -u root -p < sql/chat.sql
-- 注意：密码在服务端以 SHA-256(salt+password) 存储，不再明文

create database if not exists chat character set utf8mb4 collate utf8mb4_unicode_ci;
use chat;

drop table if exists User;
drop table if exists Friend;
drop table if exists AllGroup;
drop table if exists GroupUser;
drop table if exists OfflineMessage;

-- 用户表（salt 为密码哈希盐值，改造后新增）
create table User (
    id          int primary key auto_increment,
    name        varchar(50) not null unique,
    password    varchar(64) not null comment 'SHA-256(salt+password) 十六进制',
    salt        varchar(32) not null default '' comment '每用户随机盐值',
    state       enum('online','offline') default 'offline',
    unique key uk_name (name)
) engine=InnoDB default charset=utf8mb4 comment '用户表';

-- 好友关系表
create table Friend (
    id       int primary key auto_increment,
    userid   int not null,
    friendid int not null,
    key idx_userid (userid)
) engine=InnoDB default charset=utf8mb4 comment '好友关系表';

-- 群组表
create table AllGroup (
    id        int primary key auto_increment,
    groupname varchar(50) not null,
    groupdesc varchar(500) not null default ''
) engine=InnoDB default charset=utf8mb4 comment '群组表';

-- 群成员表
create table GroupUser (
    id      int primary key auto_increment,
    groupid int not null,
    userid  int not null,
    role    varchar(10) default 'normal' comment 'creator/normal',
    key idx_groupid (groupid),
    key idx_userid (userid)
) engine=InnoDB default charset=utf8mb4 comment '群组成员表';

-- 离线消息表
create table OfflineMessage (
    id      int primary key auto_increment,
    userid  int not null,
    message varchar(5000) not null comment 'JSON格式的未读消息',
    key idx_userid (userid)
) engine=InnoDB default charset=utf8mb4 comment '离线消息表';
