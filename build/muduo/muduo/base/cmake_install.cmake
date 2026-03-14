# Install script for directory: /home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base

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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/build/muduo/lib/libmuduo_base.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/muduo/base" TYPE FILE FILES
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/AsyncLogging.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Atomic.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/BlockingQueue.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/BoundedBlockingQueue.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Condition.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/CountDownLatch.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/CurrentThread.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Date.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Exception.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/FileUtil.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/GzipFile.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/LogFile.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/LogStream.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Logging.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Mutex.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/ProcessInfo.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Singleton.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/StringPiece.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Thread.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/ThreadLocal.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/ThreadLocalSingleton.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/ThreadPool.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/TimeZone.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Timestamp.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/Types.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/WeakCallback.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/copyable.h"
    "/home/yzy/Cluster-Chatroom/Cluster-Chatroom-project/muduo/muduo/base/noncopyable.h"
    )
endif()

