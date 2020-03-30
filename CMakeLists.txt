PROJECT(controldev_websocket)
cmake_minimum_required(VERSION 2.6)

SET (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.orogen/config")
INCLUDE(controldev_websocketBase)

if (ROCK_TEST_ENABLED)
    enable_testing()
    find_package(Syskit REQUIRED)
    syskit_orogen_tests(test)
endif()

# FIND_PACKAGE(KDL)
# FIND_PACKAGE(OCL)

