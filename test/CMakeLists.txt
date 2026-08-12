# 单元测试：会话重排器
# 不依赖 muduo/mysql/hiredis，可独立编译运行，用于验证消息有序性核心逻辑
add_executable(orderer_test orderer_test.cpp)
target_include_directories(orderer_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/thirdparty
)
