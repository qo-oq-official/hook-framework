set(HOOK_FW_SOURCES
    dllmain.cpp
    engine/hook_engine.cpp
    thunk/thunk_manager.cpp
    thunk/dispatch.cpp
    thunk/universal_bridge.cpp
    collector/raw_collector.cpp
    collector/typed_collector.cpp
    collector/collector_factory.cpp
    collector/class_cache_collector.cpp
    replacer/dll_replacer.cpp
    replacer/callback_replacer.cpp
    output/hook_data_source.cpp
    output/perfetto_tracer.cpp
    output/track_event_observer.cpp
    config/config_loader.cpp
    config/config_validator.cpp
    logging/logger.cpp
    api/hook_api.cpp
)

set(HOOK_FW_ASM_SOURCES
    thunk/thunk_x64.asm
    thunk/light_thunk_x64.asm
    thunk/universal_stub.asm
    thunk/call_original_x64.asm
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
        perfetto_sdk
        oleaut32
        ole32
        dbghelp
        ws2_32
)

target_compile_definitions(hook_framework PRIVATE HOOKFW_EXPORTS)
target_compile_options(hook_framework PRIVATE /EHa)
add_dependencies(hook_framework git_hash)

add_library(etw_monitor STATIC etw/etw_monitor.cpp)
target_include_directories(etw_monitor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(etw_monitor PUBLIC advapi32 spdlog)

add_executable(etw_monitor_demo etw/etw_monitor_main.cpp)
target_link_libraries(etw_monitor_demo PRIVATE etw_monitor)

add_executable(etw_loader
    etw/etw_loader_main.cpp
    etw/etw_loader.cpp
    etw/etw_loader_config.cpp
)
target_link_libraries(etw_loader PRIVATE etw_monitor nlohmann_json spdlog)
target_compile_options(etw_loader PRIVATE /EHsc)