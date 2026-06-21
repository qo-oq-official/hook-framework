# Injector 功能参考

## 概述

Injector 是 hook-framework 的**自定义代码注入**扩展机制，允许在目标函数被调用时执行用户自定义逻辑。与 Collector（采集原始数据）不同，Injector 支持：

- **读取** HookContext 中的参数和返回值
- **写入** Perfetto trace 自定义事件
- **Pre 模式**：在原函数执行之前运行注入逻辑
- **Post 模式**：在原函数执行之后运行注入逻辑（可观察修改后的对象状态）

---

## 快速开始

### 1. 定义注入器

```cpp
// my_injector.cpp
#include <hook_framework/injector_base.h>
#include <hook_framework/hook_framework.h>

class MyInjector : public hookfw::InjectorBase {
public:
    std::string name() const override { return "my_injector"; }

    void onInject(hookfw::HookContext& ctx, hookfw::TraceOutputWriter* trace) override {
        // 读取第 0 个参数（x64: RCX 寄存器）
        auto* obj = ctx.arg<MyStruct*>(0);

        // 写入 Perfetto 自定义事件
        if (trace) {
            nlohmann::json args;
            args["field_x"] = obj->x;
            args["field_name"] = obj->name;
            trace->addTraceEvent(
                "Injector: my_event",
                "hookfw",
                'i',  // instant event
                hookfw::TraceOutputWriter::nowMicroseconds(),
                GetCurrentProcessId(),
                GetCurrentThreadId(),
                args
            );
        }
    }
};

// 静态注册，DLL 加载时自动生效
REGISTER_INJECTOR("my_injector", MyInjector);
```

### 2. hooks.json 配置

```json
{
  "module": "sldmodu.dll",
  "symbol": "?getObject@moExtObjHelper_c@@...",
  "injector": "my_injector",
  "injector_mode": "pre",
  "action": "capture",
  "params": [
    {"name": "this", "type": "pointer"},
    {"name": "pName", "type": "wstring_ref", "capture_on_exit": true}
  ]
}
```

### 3. C++ API 方式

```cpp
#include <hook_framework/hook_framework.h>

auto& engine = hookfw::HookEngine::instance();
engine.initialize();
engine.setOutputDirectory("dump_data");

engine.hook("sldmodu.dll", "?getObject@moExtObjHelper_c@@...")
    .params({{"pName", hookfw::ParamType::WStringRef, 0, hookfw::ParamType::Pointer, false, true}})
    .returnType(hookfw::ParamType::Pointer)
    .injector("my_injector")
    .injectorMode(hookfw::InjectorMode::Pre)
    .install();

engine.installAll();
```

---

## API 参考

### InjectorBase

```cpp
namespace hookfw {

class InjectorBase {
public:
    virtual ~InjectorBase() = default;

    // 返回注入器名称（与 REGISTER_INJECTOR 宏中的 name 一致）
    virtual std::string name() const = 0;

    // 注入回调
    //   ctx   - 可变的 HookContext（寄存器、参数、返回值）
    //   trace - Perfetto 跟踪输出（可为 nullptr，仅在 Perfetto 模式有效）
    virtual void onInject(HookContext& ctx, TraceOutputWriter* trace) = 0;
};

}
```

### InjectorMode

| 值 | 含义 |
|----|------|
| `InjectorMode::Pre` | 在**原函数执行前**注入（默认） |
| `InjectorMode::Post` | 在**原函数执行后**注入 |

**Pre 模式**：注入器可观察参数修改前的状态。
**Post 模式**：注入器可观察通过指针传入的对象被原函数修改后的状态。

### HookBuilder API

```cpp
// 设置注入器名称
HookBuilder& injector(const std::string& name);

// 设置注入器模式
HookBuilder& injectorMode(InjectorMode mode);
```

### HookContext API

```cpp
struct HookContext {
    // 读取整数/指针参数
    // index: 0=RCX, 1=RDX, 2=R8, 3=R9, 4+=stack[offset]
    template<typename T> T arg(int index) const;

    // 读取返回值（仅在 Post 模式 phase==1 有效）
    template<typename T> T returnValue() const;

    // 读取浮点参数
    float  argFloat(int index) const;
    double argDouble(int index) const;

    // 修改参数（注意：当前 thunk 使用原始寄存器值，
    // 修改 ctx 不会影响实际调用参数）
    template<typename T> void setArg(int index, T value);

    // 修改返回值（注意：当前 thunk 返回原始 rax 值，
    // 修改 ctx.rax 不会影响调用者看到的返回值）
    template<typename T> void setReturnValue(T value);
};
```

### REGISTER_INJECTOR 宏

```cpp
#define REGISTER_INJECTOR(name, className)
```

在 DLL 加载时通过静态初始化自动注册注入器。编译到 `hook_framework.dll` 或链接该 DLL 的模块中均有效。

### TraceOutputWriter::addTraceEvent

```cpp
void addTraceEvent(
    const std::string& name,      // Perfetto 事件名
    const std::string& category,  // 分类（如 "hookfw"）
    char phase,                   // 'B' begin, 'E' end, 'i' instant
    double timestampUs,           // 时间戳（微秒）
    uint32_t pid,
    uint32_t tid,
    const nlohmann::json& args    // 自定义参数
);
```

**Perfetto 事件类型**：

| phase | 类型 | 用途 |
|-------|------|------|
| `'i'` | Instant | 瞬时事件，注入器最常用 |
| `'B'` | Begin | 持续事件开始 |
| `'E'` | End | 持续事件结束 |

---

## Perfetto trace 示例

注入后 `trace.json` 中出现：

```json
{"name":"Injector: my_injector","cat":"hookfw","ph":"i","ts":51081975466.6,
 "pid":22804,"tid":4312,
 "args":{"pre_x":0,"pre_flag":0,"pre_name":"hello","pre_wname":"aGVsbG8="}}
```

Perfetto UI 中显示为嵌套在目标函数 B/E 时间范围内的 instant 事件。

---

## 测试覆盖

### 测试场景矩阵

| 测试文件 | 场景 | 断言数 | 验证方式 |
|----------|------|--------|----------|
| `test_injector.cpp` | 默认 Pre 模式，注入器 + collector 共存 | 6 | 函数返回值正确 |
| `test_injector_modes.cpp` | Pre/Post 模式 + injector-only + 多注入器组合 | 12 | API 安装 + 函数返回值正确 |
| `test_injector_struct.cpp` | Pre 观察原始成员，Post 观察修改后成员 | 19 | 注入器记录值与原函数预期值比对 |

### 验证矩阵

| 验证点 | test_injector | test_injector_modes | test_injector_struct |
|--------|:---:|:---:|:---:|
| DLL 静态注册 | ✓ | ✓ | ✓ |
| `.injector()` API | ✓ | ✓ | ✓ |
| `.injectorMode()` API | — | ✓ | ✓ |
| `installAll()` 成功 | ✓ | ✓ | ✓ |
| 函数返回值不受干扰 | ✓ | ✓ | ✓ |
| Pre 在原函数之前执行 | — | — | ✓（原值 = 0） |
| Post 在原函数之后执行 | — | — | ✓（修改值 = 99） |
| `std::string` 成员可读 | — | — | ✓ |
| `std::wstring` 成员可读 | — | — | ✓ |
| HookContext::arg\<T\>() | ✓ | — | ✓ |
| HookContext::returnValue\<T\>() | — | — | ✓ |
| 多注入器共存 | — | ✓ | ✓ |

---

## 已知限制

| 限制 | 说明 |
|------|------|
| **`setArg`/`setReturnValue` 无效** | thunk 使用保存的原始寄存器，ctx 修改不反映到实际调用 |
| **同地址只一个注入器** | 同一目标地址的多个 hook 会被重复检测跳过 |
| **Pre/Post 不同函数** | 同一函数上 Pre 和 Post 注入器需分别注册到不同目标（同地址会冲突） |
| **仅 x64** | 框架仅支持 Windows x64 |
