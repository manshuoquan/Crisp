---
name: cpp-coding-standards
description: 'C++ 编码规范与 Effective C++ 实践。Use when: writing C++ code, reviewing C++ code, following Meyer/Effective C++ rules, choosing naming conventions, ensuring RAII/resource safety, avoiding undefined behavior, applying exception safety, following the CRISP project style guide.'
---

# C++ 编码规范（Effective C++ 实践）

> 适用于 CRISP 项目的所有 C++ 代码。综合 Scott Meyers《Effective C++/Modern Effective C++》、Google C++ Style、muduo 风格。
> 项目细则以 `docs/style-guide.md` 为准，本 skill 提供规范背后的原则。

## When to Use
- 编写或评审任何 `.cpp` / `.h` 文件
- 不确定命名、所有权、异常安全、const 正确性时

## 核心规则

### 1. 资源与所有权（Effective C++ 条款 13-16）
- **RAII**：资源获取即初始化。锁、句柄、文件、内存一律用 RAII 包装（`std::scoped_lock`、`unique_ptr` 等）。
- 优先 `unique_ptr`，`shared_ptr` 仅在真正共享所有权时用。
- 禁止裸 `new`/`delete`（极少数性能关键+明确注释除外）。
- 工厂函数返回 `unique_ptr`（条款 18）。

### 2. 类型与转换
- 禁 C 风格 cast。用 `static_cast`（相关类型）、`dynamic_cast`（多态下行）、`reinterpret_cast`（必须+注释）。
- 优先 `std::variant`/`std::visit` 处理异构数据，替代 `switch + cast`（Modern 条款 17）。
- 用 `std::optional` 表达"可能无值"，勿用哨兵（-1、nullptr 混用）。

### 3. const 正确性（条款 3）
- 成员函数能 const 就 const；参数能 const& 就 const&。
- 返回值：值类型用值返回（NRVO），大对象用 `const&` 或移动。
- `mutable` 仅用于逻辑 const（缓存、统计）。

### 4. 异常安全（条款 29）
- 函数提供基本/强/不抛异常保证之一，并在注释中说明。
- 析构函数、`swap`、move 操作不得抛异常（`noexcept`）。
- 构造函数中失败的资源用 RAII 成员，避免手动清理。

### 5. 类设计（条款 22-26）
- 优先组合而非继承（条款 22）。
- 接口类：虚析构 + 纯虚函数（条款 7）。
- 避免隐式类型转换，用 `explicit`（条款 15）。
- 拷贝语义：遵循 Rule of Zero/Three/Five。默认移动/拷贝被抑制时显式 `= default` 或 `= delete`。

### 6. 模板与现代特性
- 用 concepts 约束模板（C++20 之后；当前项目 C++17 用 `static_assert` + `if constexpr` 模拟）。
- `std::string_view` 做只读参数（避免 `const std::string&` 隐式分配）。
- 用 `std::chrono` 表达时间，杜绝 `int interval` 无单位时间。

### 7. 命名与可读性（CRISP 项目强制）
- 类名 `Ct` 前缀；命名空间 `crisp`。
- 成员 `m_` 前缀；函数 `lowerCamelCase`；常量 `k` 前缀。
- 公开 API 英文 Doxygen 注释。
- 函数 ≤ 60 行，单一职责。

## 常见反模式（必须避免）
- `using namespace std;` 在头文件中（禁止）
- 全局可变状态（隐藏依赖）
- 在头文件中写非 inline 函数体（ODR 违规）
- 返回悬垂引用/指针
- 用 `#define` 模拟函数（用 `constexpr`/`inline` 替代）

## 参考
- 项目细则：`docs/style-guide.md`
- 现代特性清单：`.github/skills/cpp-modern-best-practices/SKILL.md`
