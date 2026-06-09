add_executable(target_app
    target_app/target_app.cpp
)
target_compile_definitions(target_app PRIVATE TARGET_APP_EXPORTS)
target_include_directories(target_app PRIVATE target_app)
