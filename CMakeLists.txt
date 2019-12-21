cmake_minimum_required(VERSION 3.13.4)
project(slog)

set(DEPENDENCIES_DIR ".dep")
set(CMAKE_PREFIX_PATH ${CMAKE_SOURCE_DIR}/${DEPENDENCIES_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
set(CMAKE_CXX_FLAGS "-std=c++14 -W -Wextra -Wall")
set(CMAKE_CXX_FLAGS_DEBUG "-g")

find_package(ZeroMQ REQUIRED)
include_directories(${ZeroMQ_INCLUDE_DIR})
find_package(cppzmq REQUIRED)
include_directories(${cppzmq_INCLUDE_DIR})

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
if (protobuf_VERBOSE)
  message(STATUS "Using Protocol Buffers ${Protobuf_VERSION}")
endif()

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

find_package(Glog REQUIRED)
include_directories(${GLOG_INCLUDE_DIRS})

find_package(gflags REQUIRED)
include_directories(${GFLAGS_INCLUDE_DIRS})

# Add root and build directory to include path. The build directory inclusion is
# necessary for protobuf
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

# ================================== Server =============================================

# Proto needs to be added before everything else so that protobuf gets to generate files
# needed in subsequent modules
add_subdirectory(proto)
add_subdirectory(common)
add_subdirectory(connection)
add_subdirectory(storage)
set(SLOG_LIBS
  connection
  common
  proto
  storage)

# All libraries to be linked to executables
set(ALL_LIBRARIES
    ${SLOG_LIBS} 
    ${Protobuf_LIBRARIES}
    ${ZeroMQ_STATIC_LIBRARY}
    ${cppzmq_STATIC_LIBRARY}
    ${GLOG_LIBRARIES}
    ${GFLAGS_LIBRARIES})

# Create an executable for the server
add_executable(slog machine/slog.cpp)
target_link_libraries(slog ${ALL_LIBRARIES})

# =================================== Tests =============================================

enable_testing()

# Add tests
file(GLOB_RECURSE TEST_SRCS RELATIVE ${PROJECT_SOURCE_DIR} "test/*.cpp")
foreach(TEST ${TEST_SRCS})
  # Extract file names (w/o extension)
  get_filename_component(TEST_NAME ${TEST} NAME_WE)
  # Set compile target
  add_executable(${TEST_NAME} ${TEST})

  #link to libraries
  target_link_libraries(${TEST_NAME} ${ALL_LIBRARIES} GTest::GTest GTest::Main)

  add_test(
    NAME ${TEST_NAME}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${TEST_NAME})

endforeach(TEST)