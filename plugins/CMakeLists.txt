cmake_minimum_required (VERSION 2.8.7)
project("jamloop")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../bin)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

include_directories(../rtbkit ../rtbkit/leveldb/include $ENV{HOME}/local/include)
link_directories(../bin $ENV{HOME}/local/lib)
add_executable(basic_bidding_agent basic_bidding_agent.cc)
target_link_libraries(basic_bidding_agent rtb bidding_agent agent_configuration bid_request banker services arch utils jsoncpp types value_description boost_system boost_program_options)

add_library(tremor_exchange SHARED tremor_exchange_connector.cc)
target_link_libraries(tremor_exchange rtb openrtb_exchange bid_request services arch utils jsoncpp types value_description boost_system boost_thread boost_program_options)

find_package(Protobuf REQUIRED)
PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HEADER brightroll-openrtb.proto)

add_library(brightroll_exchange SHARED brightroll_exchange_connector.cc ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(brightroll_exchange protobuf rtb exchange bid_request services arch utils jsoncpp types value_description boost_system boost_thread boost_program_options)

add_library(liverail_exchange SHARED liverail_exchange_connector.cc)
target_link_libraries(liverail_exchange rtb openrtb_exchange bid_request services arch utils jsoncpp types value_description boost_system boost_thread boost_program_options)

add_subdirectory(testing)
