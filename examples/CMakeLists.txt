add_executable(target_app
    target_app/target_app.cpp
)
target_compile_definitions(target_app PRIVATE TARGET_APP_EXPORTS)
target_include_directories(target_app PRIVATE target_app)
target_link_options(target_app PRIVATE /MAP)

add_executable(rva_hook_demo
    rva_hook_demo/rva_hook_demo.cpp
    rva_hook_demo/internal_funcs.cpp
)
target_link_libraries(rva_hook_demo PRIVATE hook_framework nlohmann_json)
target_include_directories(rva_hook_demo PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/rva_hook_demo)
target_link_options(rva_hook_demo PRIVATE /MAP)

add_library(target_dll SHARED
    hook_dll_demo/target_dll.cpp
)
target_compile_definitions(target_dll PRIVATE TARGET_DLL_EXPORTS)
target_include_directories(target_dll PUBLIC hook_dll_demo)
target_link_options(target_dll PRIVATE /MAP)

add_executable(hook_dll_demo
    hook_dll_demo/hook_dll_demo.cpp
)
target_link_libraries(hook_dll_demo PRIVATE hook_framework target_dll nlohmann_json)
target_include_directories(hook_dll_demo PRIVATE hook_dll_demo ${CMAKE_SOURCE_DIR}/src)
