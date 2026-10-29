load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "raw_pdb",
    srcs = glob(
        ["src/*.cpp"],
        exclude = [
            "src/PDB_PCH.cpp",      # 排除預編譯標頭
            "src/Example*.cpp",     # 排除所有範例主程式
        ],
    ),
    hdrs = glob(["src/*.h", "src/*.inline"]),
    includes = ["src"],
    copts = select({
        "@bazel_tools//src/conditions:windows": ["/bigobj"], # 只在 Windows/MSVC 啟用 /bigobj
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
