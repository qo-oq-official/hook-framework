set(HOOK_FW_SOURCES
    dllmain.cpp
    engine/hook_engine.cpp
    injector/injector_registry.cpp
    injector/test_read_injector.cpp
    injector/test_mode_injectors.cpp
    injector/test_struct_injectors.cpp
    thunk/thunk_manager.cpp
    thunk/dispatch.cpp
    collector/raw_collector.cpp
    collector/typed_collector.cpp
    collector/collector_factory.cpp
    collector/class_cache_collector.cpp
    replacer/dll_replacer.cpp
    replacer/callback_replacer.cpp
    output/file_output_writer.cpp
    output/output_path_builder.cpp
    output/trace_output_writer.cpp
    output/trace_flusher.cpp
    config/config_loader.cpp
    config/config_validator.cpp
    logging/logger.cpp
    api/hook_api.cpp
)

set(HOOK_FW_ASM_SOURCES
    thunk/thunk_x64.asm
    thunk/light_thunk_x64.asm
)

add_library(hook_framework SHARED ${HOOK_FW_SOURCES} ${HOOK_FW_ASM_SOURCES})

target_include_directories(hook_framework
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(hook_framework
    PRIVATE
        detours
        nlohmann_json
        spdlog
        dbghelp
)

target_compile_definitions(hook_framework PRIVATE HOOKFW_EXPORTS)
target_compile_options(hook_framework PRIVATE /EHa)
add_dependencies(hook_framework git_hash)
