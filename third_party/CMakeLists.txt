file(GLOB DETOURS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Detours/src/*.cpp")
list(FILTER DETOURS_SOURCES EXCLUDE REGEX "uimports\\.cpp$")

add_library(detours STATIC ${DETOURS_SOURCES})
target_include_directories(detours PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Detours/src")
target_compile_definitions(detours PRIVATE WIN32_LEAN_AND_MEAN)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include")
target_compile_options(spdlog INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

add_library(perfetto_sdk STATIC perfetto-cpp-sdk/perfetto.cc)
target_include_directories(perfetto_sdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/perfetto-cpp-sdk")
target_compile_definitions(perfetto_sdk PUBLIC
    PERFETTO_SDK_DISABLE_SHLIB_EXPORT
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_options(perfetto_sdk PRIVATE /bigobj)
