cmake_minimum_required (VERSION 2.8.7)
project("jamloop")

macro (add_executable _name)
    _add_executable(${ARGV})
    if (TARGET ${_name})
        target_link_libraries(${_name} tcmalloc)
    endif()
endmacro()

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

add_library(jamloop_utils SHARED utils.cc)
target_link_libraries(jamloop_utils arch)

add_library(tremor_exchange SHARED tremor_exchange_connector.cc)
target_link_libraries(tremor_exchange rtb openrtb_exchange bid_request services arch utils jsoncpp types value_description boost_system boost_thread boost_program_options)

find_package(Protobuf REQUIRED)
PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HEADER brightroll-openrtb.proto)

add_library(brightroll_exchange SHARED brightroll_exchange_connector.cc ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(brightroll_exchange protobuf rtb exchange bid_request services arch utils jsoncpp types value_description boost_system boost_thread boost_program_options)

add_library(liverail_exchange SHARED liverail_exchange_connector.cc)
target_link_libraries(liverail_exchange rtb openrtb_exchange bid_request services arch utils jsoncpp types value_description jamloop_utils boost_system boost_thread boost_program_options)

set(VIEWABILITY_SERVICE_SRC viewability_augmentor.cc viewability_service.cc file_watcher.cc)
set(AUGMENTOR_LINK arch utils services types augmentor_base redis zmq jsoncpp value_description rtb bid_request gc boost_program_options agent_configuration)

add_library(viewability_service SHARED ${VIEWABILITY_SERVICE_SRC})
target_link_libraries(viewability_service ${AUGMENTOR_LINK})

add_executable(viewability_service_runner viewability_service_runner.cc)
target_link_libraries(viewability_service_runner viewability_service boost_system boost_program_options boost_thread)

add_library(adaptv_exchange SHARED adaptv_exchange_connector.cc)
target_link_libraries(adaptv_exchange rtb openrtb_exchange bid_request services arch utils jsoncpp types value_description boost_system boost_thread boost_program_options)

add_library(publisher_exchange SHARED publisher_connector.cc)
target_link_libraries(publisher_exchange rtb exchange bid_request services arch utils jsoncpp types value_description jamloop_utils boost_system boost_thread boost_program_options)

add_library(geo_pipeline SHARED geo_pipeline.cc)
target_link_libraries(geo_pipeline rtb jsoncpp)

add_library(forensiq_augmentor SHARED forensiq_augmentor.cc)
target_link_libraries(forensiq_augmentor ${AUGMENTOR_LINK})

add_executable(forensiq_augmentor_runner forensiq_augmentor_runner.cc)
target_link_libraries(forensiq_augmentor_runner forensiq_augmentor boost_system boost_program_options boost_thread)

add_subdirectory(testing)
