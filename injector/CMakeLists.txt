add_executable(injector injector.cpp)
target_include_directories(injector
    PRIVATE
        ${CMAKE_BINARY_DIR}
)
target_compile_definitions(injector PRIVATE WIN32_LEAN_AND_MEAN)
add_dependencies(injector git_hash)
