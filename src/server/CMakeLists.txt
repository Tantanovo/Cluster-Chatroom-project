#定义了一个SRC_LIST 变量 ，包含了该项目下的所有源文件
aux_source_directory(. SRC_LIST)
aux_source_directory(./mysql MYSQL_LIST)
aux_source_directory(./muduo MUDUO_LIST)
#指定可执行文件
add_executable(chatserver ${SRC_LIST} ${MYSQL_LIST} ${MUDUO_LIST})
#制定可执行性文件链接需要依赖的库文件
target_link_libraries(chatserver muduo_net muduo_base mysqlclient pthread)
