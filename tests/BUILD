# tests/BUILD

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

# ==========================================
# 1. 產生動態庫：test_pdb_target.dll
# ==========================================
cc_binary(
    name = "test_pdb_target",
    srcs = [
        "test_pdb_target.cpp",
        # 如果有平級的頭檔案，建議補上：
        # "test_pdb_target.h", 
    ],
    linkshared = True,
    defines = [
        "TEST_PDB_EXPORTS",
    ],
    copts = [
        "/std:c++17",  # 強制保持 C++17 標準的一致性
        "/Oy-",        # 關閉框架指標優化
        "/Zi",         # 生成編譯期偵錯符號
        "/FS",
        "/EHa",        # 啟用 C++ 與 Windows 結構化異常處理 (SEH)
    ],
    linkopts = [
        "/DEBUG:FULL",  # 對應 CMake 的連結選項，產生完整 PDB 偵錯檔案
    ],
)

# ==========================================
# 2. 測試執行檔：test_pdb_runner.exe
# ==========================================
cc_binary(
    name = "test_pdb_runner",
    srcs = ["test_pdb_runner.cpp"],
    copts = [
        "/std:c++17",
        "/Oy-",
        "/Z7",         # 生成編譯期偵錯符號
        "/FS",
        "/EHs",
    ],
    linkopts = [
        "/DEBUG:FULL",
    ],
    deps = [
        # 讓執行檔自動依賴並連結上面產生的動態庫
        ":test_pdb_target",
    ],
)

# ==========================================
# 3. 產生動態庫：test_etw_loader.dll
# ==========================================
cc_binary(
    name = "test_etw_loader",
    srcs = [
        "test_etw_loader.cpp",
        # "test_etw_loader.h",
    ],
    linkshared = True,
    copts = [
        "/std:c++17",
        "/Oy-",
        "/Z7",
        "/EHs",
    ],
    linkopts = [
        "/DEBUG:FULL",
    ],
)
