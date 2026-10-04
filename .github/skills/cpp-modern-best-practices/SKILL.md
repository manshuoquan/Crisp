---
name: cpp-modern-best-practices
description: '现代 C++（C++17/20）最佳实践与代码模式。Use when: writing modern C++ code, using std::variant/optional/string_view/chrono, designing event systems, using smart pointers, applying value semantics, zero-allocation hot paths, coroutines, structured bindings, if constexpr.'
---

# 现代 C++ 最佳实践（Modern C++ Patterns）

> 适用于 CRISP 项目。当前工具链：MinGW-w64 8.1.0，**语言标准锁定 C++17**（`__cplusplus == 201703L`）。
> C++20 特性（concepts/ranges/协程/format/span）在 GCC 升级前不可用，见"延迟启用"章节。

## When to Use
- 实现事件系统、队列、分发器、定时器等核心组件
- 需要值语义、类型安全分发、零拷贝、零堆分配热路径

## 核心模式

### 1. 值语义事件 + `std::variant` 类型安全分发
```cpp
// 事件 = 类型标签 + 负载值，无基类、无虚函数、无手工编号
namespace crisp::event {
template <typename T> struct CtEvent {
    T payload;
    bool accepted = false;
};

// 事件集合（编译期穷尽检查）
using CtAnyEvent = std::variant<CtEvent<MousePress>, CtEvent<KeyPress>, CtEvent<Timer>>;

// 分发：std::visit 保证穷尽；漏掉类型编译期报错
void handle(const CtAnyEvent& ev) {
    std::visit([](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, CtEvent<MousePress>>) {
            // ...
        } else if constexpr (std::is_same_v<T, CtEvent<Timer>>) {
            // ...
        }
    }, ev);
}
}
```

### 2. 类型擦除的两种层次
- **优先模板**：编译期类型，零开销。
- **需要异构容器时**：`std::variant`（有界、栈上）。
- **需要完全擦除时**：`std::function`（注意 SBO 小对象优化）或 `std::any`（慎用，需 cast）。

### 3. 零堆分配热路径
- 小对象值语义直接传值/`const&`。
- 大负载用 `unique_ptr` 转移所有权，或对象池。
- 避免在热路径创建 `std::function`/`std::string`（分配）。
- 用 `std::string_view` 做只读参数。

### 4. 时间用 `std::chrono`（杜绝 int 毫秒）
```cpp
using CtDuration = std::chrono::milliseconds;
timer_source.schedule(CtDuration{500}, handler);   // 类型安全，单位明确
// 而非 timer_source.schedule(500, handler);
```

### 5. 所有权与生命周期
- 接收者/资源用 `shared_ptr`+`weak_ptr` 或 token 注册表，杜绝悬垂。
- 队列持有 `unique_ptr`/值；出队即所有权转移。
- `weak_ptr::lock()` 检查对象是否存活。

### 6. 线程
- 用 `std::mutex` + `std::scoped_lock`（RAII 锁）。
- 条件变量 + `std::unique_lock`。
- 跨线程通信：队列 + 原子标志，避免裸共享状态。
- 优先 `std::jthread`（C++20；当前 C++17 用 `std::thread` + 手动 join/RAII 包装）。

### 7. 编译期工具
- `if constexpr`：按类型特化行为。
- `static_assert`：约束模板参数（concepts 的 C++17 替代）。
- 折叠表达式：变参注册/拼接。

## 延迟启用（等 GCC ≥ 9/11 升级）
| 特性 | 版本 | 备注 |
|------|------|------|
| `std::filesystem` | 17 | **GCC 8 有 bug**（`fs_path.h` 缺 `operator!=`），当前禁止 |
| concepts / requires | 20 | GCC 10+ |
| ranges | 20 | GCC 10+ |
| 协程 `co_await` | 20 | GCC 11+ |
| `std::span` | 20 | GCC 10+ |
| `std::format` | 20 | GCC 13+ |

## 参考
- 事件系统设计：`docs/event-system/03-rewrite-proposal.md`
- 项目细则：`docs/style-guide.md`
- 规范原则：`.github/skills/cpp-coding-standards/SKILL.md`
