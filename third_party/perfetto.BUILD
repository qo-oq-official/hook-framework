load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "perfetto_sdk",
    srcs = ["perfetto.cc"],
    hdrs = ["perfetto.h"],
    includes = ["."],
    defines = [
        "PERFETTO_SDK_DISABLE_SHLIB_EXPORT",
        "DCHECK_ALWAYS_ON",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
    ],
    copts = select({
        "@bazel_tools//src/conditions:windows": [
            "/bigobj",
            "/std:c++17",          # 強制 MSVC 開啟 C++17 標準
            "/Zc:__cplusplus",     # 修正 MSVC 內建的 __cplusplus 巨集值
            "/permissive-",        # 開啟嚴格的標準 C++ 符合性模式
            "/wd4068",             # 忽略未知的 pragma 警告（Perfetto 內含大量 GCC 特有 pragma）
            "/wd4244",             # 忽略隱式類型轉換數據遺失警告
            "/wd4267",             # 忽略 size_t 轉 int 的警告
            "/wd4996",             # 忽略 POSIX 函數不安全警告
        ],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
