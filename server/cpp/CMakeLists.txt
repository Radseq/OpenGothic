cmake_minimum_required(VERSION 3.20)

project(OpenGothicMmoServerCpp LANGUAGES CXX)

add_executable(mmo_udp_server
  mmo_udp_server.cpp
  mmo_server_combat_animation_profile.cpp
  mmo_server_combat_range_model.cpp
  mmo_server_combat_spatial_authority.cpp
  mmo_server_combat_timeline_authority.cpp
  mmo_server_damage_calculator.cpp
  mmo_server_fight_intent_authority.cpp
  mmo_server_fight_move_model.cpp
  mmo_server_conversation_authority.cpp
  mmo_server_dialog_authority.cpp
  mmo_server_movement_authority.cpp
  mmo_server_npc_activity_authority.cpp
  mmo_server_npc_action_authority.cpp
  mmo_server_perception_authority.cpp
  mmo_server_perception_queue.cpp
  mmo_server_perception_reaction_planner.cpp
  mmo_server_perception_sensor.cpp
  mmo_server_perception_witness_registry.cpp
  mmo_server_persistence.cpp
  mmo_server_quest_authority.cpp
  mmo_server_ranged_combat_authority.cpp
  mmo_server_script_authority.cpp
  mmo_server_waypoint_authority.cpp
  mmo_server_waypoint_bootstrap.cpp
  mmo_server_waypoint_graph.cpp
  mmo_server_world_clock.cpp
)

target_compile_features(mmo_udp_server PRIVATE cxx_std_23)
target_compile_definitions(mmo_udp_server PRIVATE ASIO_STANDALONE)
if(WIN32)
  target_compile_definitions(mmo_udp_server PRIVATE _WIN32_WINNT=0x0601)
endif()
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
