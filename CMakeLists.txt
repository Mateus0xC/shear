cmake_minimum_required(VERSION 3.24)
project(Shear VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)

set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "Dependências baixadas por setup-deps.ps1")

# --- core: funções puras (sem GUI), testáveis ---
add_library(shear_core STATIC
  src/core/CutPlanner.cpp
  src/core/ExportEstimate.cpp
  src/core/FfmpegCommandBuilder.cpp
  src/core/OutputNaming.cpp)
target_include_directories(shear_core PUBLIC src)
target_link_libraries(shear_core PUBLIC Qt6::Core)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE shear_core Qt6::Test)
add_test(NAME core COMMAND test_core)

add_executable(bench_export tests/bench_export.cpp)
target_link_libraries(bench_export PRIVATE shear_core)

# teclas de navegação (Shift/Ctrl/Alt + setas): teste que simula as teclas dentro do Qt, fora da tela
add_executable(test_navkeys tests/test_navkeys.cpp src/app/NavKeys.cpp)
target_include_directories(test_navkeys PRIVATE src/app)
target_link_libraries(test_navkeys PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME navkeys COMMAND test_navkeys)
set_tests_properties(navkeys PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(EXISTS "${CMAKE_SOURCE_DIR}/src/app/CMakeLists.txt")
  add_subdirectory(src/app)
endif()
