cmake_minimum_required(VERSION 3.20)
project(fbpp_codegen_smoke LANGUAGES CXX)

enable_testing()

find_package(fbpp CONFIG REQUIRED COMPONENTS codegen)

add_executable(smoke smoke.cpp)
target_compile_features(smoke PRIVATE cxx_std_20)
target_link_libraries(smoke PRIVATE fbpp::fbpp_codegen)

add_test(NAME smoke_run COMMAND smoke)
