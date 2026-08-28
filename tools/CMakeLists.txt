add_executable(dumpbin_resolver
    dumpbin_resolver/dumpbin_resolver.cpp
)

target_include_directories(dumpbin_resolver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dumpbin_resolver
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(dumpbin_resolver PRIVATE nlohmann_json)

# 将 Perfetto trace 中记录的 (模块, RVA) 符号化为函数名 + file:line
add_executable(symbolize_trace
    symbolize_trace/main.cpp
    symbolize_trace/pdb_resolver.cpp
    symbolize_trace/trace_reader.cpp
    symbolize_trace/trace_augment.cpp
)

target_include_directories(symbolize_trace PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/symbolize_trace
)

target_link_libraries(symbolize_trace PRIVATE
    raw_pdb
    spdlog
    nlohmann_json
    dbghelp
)

target_compile_options(symbolize_trace PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
