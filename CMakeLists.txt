cmake_minimum_required(VERSION 3.20)
project(hook_framework LANGUAGES CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

add_custom_target(git_hash ALL
    ${CMAKE_COMMAND} -D SRC=${CMAKE_SOURCE_DIR} -D DST=${CMAKE_BINARY_DIR}/generated/git_hash.h
                     -P ${CMAKE_SOURCE_DIR}/cmake/gen_git_hash.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/git_hash.h
    COMMENT "Generating git hash header..."
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(third_party)
add_subdirectory(src)
add_subdirectory(tools)
add_subdirectory(injector)
add_subdirectory(tests)
