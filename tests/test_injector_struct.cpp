#include "hook_framework/hook_framework.h"
#include "test_struct_target.h"
#include "test_injector_vals.h"
#include <Windows.h>
#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  [PASS] %s\n", msg); g_pass++; } \
    else { printf("  [FAIL] %s\n", msg); g_fail++; } \
} while(0)

typedef void (*ModifyFunc)(TestObjectA*);
typedef void (*ModifyFunc2)(TestObjectA*);

int main() {
    printf("=== Struct Injector Test ===\n");
    printf("Tests: Pre observes original members, Post observes modified members\n\n");

    HMODULE hTarget = LoadLibraryA("test_target.dll");
    CHECK(hTarget != nullptr, "test_target.dll loaded");
    if (!hTarget) return 1;

    auto test_modify = reinterpret_cast<ModifyFunc>(
        GetProcAddress(hTarget, "test_modify_object"));
    auto test_modify2 = reinterpret_cast<ModifyFunc2>(
        GetProcAddress(hTarget, "test_modify_object2"));
    CHECK(test_modify != nullptr, "test_modify_object resolved");
    CHECK(test_modify2 != nullptr, "test_modify_object2 resolved");

    printf("[1] Setting up Pre on test_modify_object, Post on test_modify_object2...\n");
    auto& engine = hookfw::HookEngine::instance();
    engine.initialize();
    engine.setOutputDirectory("dump_data_struct");

    bool ok1 = engine.hook("test_target.dll", "test_modify_object")
        .injector("pre_struct_injector")
        .injectorMode(hookfw::InjectorMode::Pre)
        .install();
    CHECK(ok1, "Pre struct injector installed on test_modify_object");

    bool ok2 = engine.hook("test_target.dll", "test_modify_object2")
        .injector("post_struct_injector")
        .injectorMode(hookfw::InjectorMode::Post)
        .install();
    CHECK(ok2, "Post struct injector installed on test_modify_object2");

    CHECK(engine.installAll(), "installAll succeeded");

    printf("\n[2] Initializing object A with original values...\n");
    TestObjectA obj;
    obj.x     = 0;
    obj.y     = 0.0;
    obj.flag  = '\0';
    obj.name  = "hello";
    obj.wname = L"\u4f60\u597d";

    printf("\n[3] Calling test_modify_object(&obj) -> Pre injector fires...\n");
    g_values.reset();
    test_modify(&obj);

    printf("\n[4] Verifying Pre injector observed ORIGINAL values...\n");
    CHECK(g_values.pre_x.load()     == 0,         "Pre x == 0 (before modify)");
    CHECK(g_values.pre_flag.load()  == 0,         "Pre flag == '\\0'");
    CHECK(g_values.pre_name         == "hello",   "Pre name == 'hello'");
    CHECK(g_values.pre_wname        == L"\u4f60\u597d", "Pre wname == original wide");

    printf("\n[5] Verifying original function modified obj...\n");
    CHECK(obj.x     == 42,          "obj.x == 42");
    CHECK(obj.flag  == 'Z',         "obj.flag == 'Z'");
    CHECK(obj.name  == "modified",  "obj.name == 'modified'");
    CHECK(obj.wname == L"\u5df2\u4fee\u6539", "obj.wname == modified");

    printf("\n[6] Resetting obj, calling test_modify_object2(&obj) -> Post injector fires...\n");
    obj.x     = 10;
    obj.y     = 1.0;
    obj.flag  = 'A';
    obj.name  = "before";
    obj.wname = L"\u524d";
    g_values.reset();
    test_modify2(&obj);

    printf("\n[7] Verifying Post injector observed MODIFIED values...\n");
    CHECK(g_values.post_x.load()    == 99,        "Post x == 99 (after modify2)");
    CHECK(g_values.post_flag.load() == 'X',       "Post flag == 'X'");
    CHECK(g_values.post_name        == "modified2","Post name == 'modified2'");
    CHECK(g_values.post_wname       == L"\u4fee\u65392", "Post wname == modified2 wide");
    CHECK(obj.x == 99 && obj.name == "modified2", "Post reads same values obj ends with");

    engine.shutdown();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail;
}
