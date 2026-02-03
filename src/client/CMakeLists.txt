#定义了一个变量，包含当前目录下的所有源文件
aux_source_directory(. SRC_LIST)
#指定生成可执行文件
add_executable(chatclient ${SRC_LIST})
#指定可执行文件链接的库
target_link_libraries(chatclient  pthread)