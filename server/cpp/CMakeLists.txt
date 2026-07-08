cmake_minimum_required(VERSION 3.21)

project(OpenGothicMmoServerTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET zenkit)
  set(OPENGOTHIC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "OpenGothic repository root")
  if(EXISTS "${OPENGOTHIC_ROOT}/lib/ZenKit/CMakeLists.txt")
    add_subdirectory("${OPENGOTHIC_ROOT}/lib/ZenKit" "${CMAKE_BINARY_DIR}/ZenKit")
  else()
    message(FATAL_ERROR "ZenKit target not found. Configure from the full OpenGothic repo with lib/ZenKit present.")
  endif()
endif()

add_library(mmo_content_build_loader STATIC
  mmo_content_build_loader.cpp
)
target_include_directories(mmo_content_build_loader PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(mmo_content_build_loader PUBLIC
  zenkit
)

add_executable(mmo_content_build_importer
  mmo_content_build_importer.cpp
)
target_link_libraries(mmo_content_build_importer PRIVATE
  mmo_content_build_loader
)

add_library(mmo_runtime_read_model_loader STATIC
  mmo_runtime_read_model_loader.cpp
)
target_include_directories(mmo_runtime_read_model_loader PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)

add_executable(mmo_runtime_read_model_probe
  mmo_runtime_read_model_probe.cpp
)
target_link_libraries(mmo_runtime_read_model_probe PRIVATE
  mmo_runtime_read_model_loader
)

add_library(mmo_world_instance_content_cache STATIC
  mmo_world_instance_content_cache.cpp
)
target_include_directories(mmo_world_instance_content_cache PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(mmo_world_instance_content_cache PUBLIC
  mmo_runtime_read_model_loader
)

add_executable(mmo_world_instance_content_cache_probe
  mmo_world_instance_content_cache_probe.cpp
)
target_link_libraries(mmo_world_instance_content_cache_probe PRIVATE
  mmo_world_instance_content_cache
)

add_library(mmo_server_persistence STATIC
  mmo_server_persistence.cpp
)
target_include_directories(mmo_server_persistence PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)

add_library(mmo_npc_perception_policy STATIC
  mmo_npc_perception_policy.cpp
)
target_include_directories(mmo_npc_perception_policy PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(mmo_npc_perception_policy PUBLIC
  mmo_world_instance_content_cache
)

add_executable(mmo_npc_perception_policy_probe
  mmo_npc_perception_policy_probe.cpp
)
target_link_libraries(mmo_npc_perception_policy_probe PRIVATE
  mmo_npc_perception_policy
)

add_library(mmo_npc_perception_runtime_source STATIC
  mmo_npc_perception_runtime_source.cpp
)
target_include_directories(mmo_npc_perception_runtime_source PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(mmo_npc_perception_runtime_source PUBLIC
  mmo_npc_perception_policy
  mmo_server_persistence
)

add_library(mmo_ai_runtime_persistence STATIC
  mmo_ai_runtime_persistence.cpp
)
target_include_directories(mmo_ai_runtime_persistence PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(mmo_ai_runtime_persistence PUBLIC
  mmo_npc_perception_policy
  mmo_server_persistence
)

add_executable(mmo_npc_perception_record_probe
  mmo_npc_perception_record_probe.cpp
)
target_link_libraries(mmo_npc_perception_record_probe PRIVATE
  mmo_npc_perception_runtime_source
  mmo_ai_runtime_persistence
)

add_executable(mmo_udp_server
  mmo_udp_server.cpp
)
target_compile_definitions(mmo_udp_server PRIVATE
  ASIO_STANDALONE
)
if(WIN32)
  target_compile_definitions(mmo_udp_server PRIVATE _WIN32_WINNT=0x0601)
endif()
target_include_directories(mmo_udp_server SYSTEM PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/asio/include"
)
target_include_directories(mmo_udp_server PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../../game/game"
)
find_package(Threads REQUIRED)
target_link_libraries(mmo_udp_server PRIVATE
  Threads::Threads
  mmo_world_instance_content_cache
)

add_executable(mmo_vdf_world_zen_probe
  mmo_vdf_world_zen_probe.cpp
)
target_link_libraries(mmo_vdf_world_zen_probe PRIVATE
  zenkit
)
