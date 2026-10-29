
cc_library(
    name = "detours",
    srcs = glob(
        ["src/*.cpp"],
        exclude = ["src/uimports.cpp"], # 完美替代 CMake 的 list(FILTER...EXCLUDE)
    ),
    hdrs = glob(["src/*.h"]),
    includes = ["src"], # 替代 target_include_directories PUBLIC
    defines = ["WIN32_LEAN_AND_MEAN"], # 替代 target_compile_definitions PRIVATE
    visibility = ["//visibility:public"],
)
