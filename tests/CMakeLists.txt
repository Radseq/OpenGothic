cmake_minimum_required(VERSION 3.28)

project(OpenGothicMmoClientInventoryUiTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

function(opengothic_mmo_client_ui_test target source)
  if(NOT TARGET gothic_mmo_client_facade_headers)
    message(FATAL_ERROR
      "Configure client UI integration tests from the repository root")
  endif()
  add_executable(${target} ${source})
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../game")
  target_link_libraries(${target} PRIVATE
    gothic_mmo_client_facade_headers)
  target_compile_definitions(${target} PRIVATE
    OPENGOTHIC_MMO_SANDBOX_FACADE=1)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Werror)
  endif()

  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_executable(l01_server_inventory_ui_model_tests
  mmo_server_inventory_ui_model_tests.cpp)
target_include_directories(l01_server_inventory_ui_model_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../game")

if(MSVC)
  target_compile_options(l01_server_inventory_ui_model_tests PRIVATE
    /W4 /WX /permissive-)
else()
  target_compile_options(l01_server_inventory_ui_model_tests PRIVATE
    -Wall -Wextra -Wconversion -Werror)
endif()

add_test(NAME l01_server_inventory_ui_model_tests
  COMMAND l01_server_inventory_ui_model_tests)

add_executable(native_single_player_telemetry_tests
  native_telemetry_test.cpp
  ../game/utils/nativetelemetry.cpp)
target_include_directories(native_single_player_telemetry_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../game")

if(MSVC)
  target_compile_options(native_single_player_telemetry_tests PRIVATE
    /W4 /WX /permissive-)
else()
  target_compile_options(native_single_player_telemetry_tests PRIVATE
    -Wall -Wextra -Wconversion -Werror)
endif()

add_test(NAME native_single_player_telemetry_tests
  COMMAND native_single_player_telemetry_tests)

opengothic_mmo_client_ui_test(
  l05_client_corpse_loot_adapter_tests
  mmoclientcorpselootadapter_test.cpp)
opengothic_mmo_client_ui_test(
  l05_corpse_loot_facade_projection_tests
  mmocorpselootfacadeprojection_test.cpp)
opengothic_mmo_client_ui_test(
  l05_server_corpse_loot_read_model_tests
  mmoservercorpselootreadmodel_test.cpp)
