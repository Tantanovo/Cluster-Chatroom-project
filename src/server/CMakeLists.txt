#定义了一个SRC_LIST 变量 ，包含了该项目下的所有源文件
aux_source_directory(. SRC_LIST)
aux_source_directory(./db DB_LIST)
aux_source_directory(./muduo MUDUO_LIST)
aux_source_directory(./redis REDIS_LIST)

#指定可执行文件
add_executable(chatserver ${SRC_LIST} ${DB_LIST} ${MUDUO_LIST} ${REDIS_LIST})
#制定可执行性文件链接需要依赖的库文件
target_link_libraries(chatserver muduo_net muduo_base mysqlclient hiredis pthread crypto ssl)
