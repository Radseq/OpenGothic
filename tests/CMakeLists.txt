cmake_minimum_required(VERSION 3.28)

project(OpenGothicMmoClientInventoryUiTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

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
