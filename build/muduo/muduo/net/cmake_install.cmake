# Install script for directory: /home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/build/muduo/lib/libmuduo_net.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/muduo/net" TYPE FILE FILES
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/Buffer.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/Callbacks.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/Channel.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/Endian.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/EventLoop.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/EventLoopThread.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/EventLoopThreadPool.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/InetAddress.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/TcpClient.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/TcpConnection.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/TcpServer.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/net/TimerId.h"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/build/muduo/muduo/net/http/cmake_install.cmake")
  include("/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/build/muduo/muduo/net/inspect/cmake_install.cmake")

endif()

