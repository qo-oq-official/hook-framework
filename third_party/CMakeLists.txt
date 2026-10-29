cmake_minimum_required(VERSION 3.14)
project(MyProject LANGUAGES C CXX)

# 啟用全域屬性：讓其他依賴項也能正確識別 IDE（如 VS）中的資料夾結構
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# 引入 CMake 內置的 FetchContent 模組
include(FetchContent)

# ==============================================================================
# 1. nlohmann_json
# ==============================================================================
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ==============================================================================
# 2. spdlog
# ==============================================================================
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(spdlog)
# 為 spdlog 介面補上 MSVC 的 UTF-8 編碼選項
target_compile_options(spdlog INTERFACE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
target_compile_definitions(spdlog PUBLIC SPDLOG_WCHAR_TO_UTF8_SUPPORT)

# ==============================================================================
# 3. Microsoft Detours
# ==============================================================================
FetchContent_Declare(
    detours_fetch
    GIT_REPOSITORY https://github.com/microsoft/Detours.git
    GIT_TAG        d644ce94e8c7f7f5a31591577c78134ea3ac1fae
)
FetchContent_GetProperties(detours_fetch)
if(NOT detours_fetch_POPULATED)
    FetchContent_Populate(detours_fetch)

    # 根據 Zip 解壓結構，源碼實際位於 src/ 子目錄下
    set(DETOURS_SRC_DIR "${detours_fetch_SOURCE_DIR}/src")

    # 匹配你原本的 GLOB 篩選邏輯，只抓取核心編譯所需的 cpp
    file(GLOB DETOURS_SOURCES "${DETOURS_SRC_DIR}/*.cpp")
    list(FILTER DETOURS_SOURCES EXCLUDE REGEX "uimports\\.cpp$")

    add_library(detours STATIC ${DETOURS_SOURCES})
    target_include_directories(detours PUBLIC "${DETOURS_SRC_DIR}")
    target_compile_definitions(detours PRIVATE WIN32_LEAN_AND_MEAN)
endif()

# ==============================================================================
# 4. raw_pdb (微軟 PDB 檔案解析庫)
# ==============================================================================
FetchContent_Declare(
    raw_pdb_fetch
    GIT_REPOSITORY https://github.com/MolecularMatters/raw_pdb
    GIT_TAG        main
)
FetchContent_GetProperties(raw_pdb_fetch)
if(NOT raw_pdb_fetch_POPULATED)
    FetchContent_Populate(raw_pdb_fetch)

    # 核心源碼位於倉庫的 src/ 目錄中
    set(RAW_PDB_SRC_DIR "${raw_pdb_fetch_SOURCE_DIR}/src")

    # 使用 file(GLOB) 掃描官方 src 目錄
    file(GLOB RAW_PDB_SOURCES "${RAW_PDB_SRC_DIR}/*.cpp")

    # 【關鍵安全過濾】：排除官方的預編譯標頭、以及所有帶有 Example 前綴的範例項目主程式
    list(FILTER RAW_PDB_SOURCES EXCLUDE REGEX "PDB_PCH\\.cpp$")
    list(FILTER RAW_PDB_SOURCES EXCLUDE REGEX "Example.*\\.cpp$")

    # 建立獨立的靜態庫 Target
    add_library(raw_pdb STATIC ${RAW_PDB_SOURCES})
    target_include_directories(raw_pdb PUBLIC "${RAW_PDB_SRC_DIR}")
    target_compile_options(raw_pdb PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()

# ==============================================================================
# 5. Perfetto SDK (效能分析工具 SDK - 從官方 URL 網址自動下載 Zip 構建)
# ==============================================================================
FetchContent_Declare(
    perfetto_sdk_fetch
    URL      https://github.com/google/perfetto/releases/download/v59.1/perfetto-cpp-sdk-src.zip
    URL_HASH SHA256=79951246078ea0406eb4c7c6bb71dbbc31849d5dc13de2c0697995c7acfd0b04
)
FetchContent_GetProperties(perfetto_sdk_fetch)
if(NOT perfetto_sdk_fetch_POPULATED)
    FetchContent_Populate(perfetto_sdk_fetch)

    # 直接編譯 Zip 包解壓出來的唯一 perfetto.cc
    add_library(perfetto_sdk STATIC "${perfetto_sdk_fetch_SOURCE_DIR}/perfetto.cc")
    target_include_directories(perfetto_sdk PUBLIC "${perfetto_sdk_fetch_SOURCE_DIR}")

    # 帶入你原本專案指定的巨集與編譯參數
    target_compile_definitions(perfetto_sdk PUBLIC
        PERFETTO_SDK_DISABLE_SHLIB_EXPORT
        DCHECK_ALWAYS_ON
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_compile_options(perfetto_sdk PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/bigobj>)
endif()
