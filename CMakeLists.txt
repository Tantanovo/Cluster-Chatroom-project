cmake_minimum_required(VERSION 3.0)
project(chat)
#配置编译器选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g -std=c++11)

#配置最终的可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/server
    ${PROJECT_SOURCE_DIR}/include/server/db
    ${PROJECT_SOURCE_DIR}/include/server/redis
    ${PROJECT_SOURCE_DIR}/include/server/model
    ${PROJECT_SOURCE_DIR}/thirdparty
)
add_subdirectory(muduo)
#加载子目录
add_subdirectory(src)
#单元测试（不依赖第三方库，可独立编译）
add_subdirectory(test)
