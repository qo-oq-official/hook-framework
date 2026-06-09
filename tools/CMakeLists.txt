add_executable(dumpbin_resolver
    dumpbin_resolver/dumpbin_resolver.cpp
)

target_include_directories(dumpbin_resolver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dumpbin_resolver
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dumpbin_resolver PRIVATE nlohmann_json)
