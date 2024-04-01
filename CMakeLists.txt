project(lua-protobuf)
cmake_minimum_required(VERSION 3.17)
add_library(pb SHARED pb.c)

# set(LUA_LIBRARIES ../lua)
# set(LUA_INCLUDE_DIR ../lua)
# include_directories(${LUA_INCLUDE_DIR})

set_target_properties(pb PROPERTIES PREFIX "")
