add_library(test_target SHARED test_target.cpp)
target_compile_definitions(test_target PRIVATE TEST_TARGET_EXPORTS)
target_compile_options(test_target PRIVATE /Oy-)

add_library(test_scale_target SHARED test_scale_target.cpp)
target_compile_definitions(test_scale_target PRIVATE TEST_SCALE_EXPORTS)

add_executable(test_capture test_capture.cpp)
target_link_libraries(test_capture PRIVATE hook_framework nlohmann_json)
target_include_directories(test_capture PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test_replace test_replace.cpp)
target_link_libraries(test_replace PRIVATE hook_framework)
target_include_directories(test_replace PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test_replace_stack test_replace_stack.cpp)
target_link_libraries(test_replace_stack PRIVATE hook_framework)
target_include_directories(test_replace_stack PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)

add_executable(test_injection test_injection.cpp)
target_link_libraries(test_injection PRIVATE nlohmann_json)
target_compile_definitions(test_injection PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(test_live_inject test_live_inject.cpp)
target_link_libraries(test_live_inject PRIVATE nlohmann_json)
target_compile_definitions(test_live_inject PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(test_multi_dll test_multi_dll.cpp)
target_link_libraries(test_multi_dll PRIVATE hook_framework nlohmann_json)
target_include_directories(test_multi_dll PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test_rva_hook test_rva_hook.cpp)
target_link_libraries(test_rva_hook PRIVATE hook_framework nlohmann_json)
target_include_directories(test_rva_hook PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test_deref_output test_deref_output.cpp)
target_link_libraries(test_deref_output PRIVATE hook_framework nlohmann_json)
target_include_directories(test_deref_output PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)

add_executable(test_deref_string test_deref_string.cpp)
target_link_libraries(test_deref_string PRIVATE hook_framework nlohmann_json)
target_include_directories(test_deref_string PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)

add_executable(test_rva_display test_rva_display.cpp)
target_link_libraries(test_rva_display PRIVATE hook_framework nlohmann_json)
target_include_directories(test_rva_display PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(test_rva_integration test_rva_integration.cpp)
target_link_libraries(test_rva_integration PRIVATE nlohmann_json)
target_compile_definitions(test_rva_integration PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(test_injector test_injector.cpp)
target_link_libraries(test_injector PRIVATE hook_framework nlohmann_json)
target_include_directories(test_injector PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(test_injector PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(test_injector_modes test_injector_modes.cpp)
target_link_libraries(test_injector_modes PRIVATE hook_framework nlohmann_json)
target_include_directories(test_injector_modes PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(test_injector_modes PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(test_injector_struct test_injector_struct.cpp)
target_link_libraries(test_injector_struct PRIVATE hook_framework nlohmann_json)
target_include_directories(test_injector_struct PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/injector ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(test_injector_struct PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(test_perfetto_params test_perfetto_params.cpp)
target_link_libraries(test_perfetto_params PRIVATE hook_framework)
target_include_directories(test_perfetto_params PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(test_perfetto_params PRIVATE WIN32_LEAN_AND_MEAN)

add_executable(test_rva_scale test_rva_scale.cpp)
target_link_libraries(test_rva_scale PRIVATE hook_framework)
target_include_directories(test_rva_scale PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)

# ── PDB test targets ────────────────────────────────────────────────────

add_library(test_pdb_target SHARED test_pdb_target.cpp)
target_compile_definitions(test_pdb_target PRIVATE TEST_PDB_EXPORTS)
target_compile_options(test_pdb_target PRIVATE /Oy- /Zi)
target_link_options(test_pdb_target PRIVATE /DEBUG:FULL)

add_executable(test_pdb_runner test_pdb_runner.cpp)
