cmake_minimum_required(VERSION 3.20)

project(OpenGothicMmoServerCpp LANGUAGES CXX)

add_executable(mmo_udp_server
  mmo_udp_server.cpp
)

target_compile_features(mmo_udp_server PRIVATE cxx_std_23)
target_compile_definitions(mmo_udp_server PRIVATE ASIO_STANDALONE)
target_include_directories(mmo_udp_server SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/asio/include
)
target_include_directories(mmo_udp_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../game/game
)

if(MSVC)
  target_compile_options(mmo_udp_server PRIVATE /W4 /permissive-)
else()
  target_compile_options(mmo_udp_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
target_link_libraries(mmo_udp_server PRIVATE Threads::Threads)
