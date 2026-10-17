---
name: cpp-review-rules
description: 'C++ 代码审查规则（Effective C++ / Modern Effective C++ / C++ Core Guidelines / Google Style 综合）。Use when: reviewing C++ code, auditing code quality, finding bugs/UB/leaks/thread issues, grading findings by severity (must-fix / warning / suggestion), running the code auditor over M1-M4 deliverables.'
---

# C++ 代码审查规则（Code Review Rules）

> 适用于 CRISP 项目的所有 C++ 代码审查。综合 Scott Meyers《Effective C++》(3rd, 55 Items) /《Effective Modern C++》(42 Items)、C++ Core Guidelines（isocpp.github.io/CppCoreGuidelines/）、Google C++ Style Guide、muduo 风格。
> 项目硬性细则以 `AGENTS.md` 与 `docs/style-guide.md` 为准；本 skill 提供审查时逐条检查的**可执行清单**与**问题分级**。
> 工具链约束：MinGW-w64 8.1.0，**C++17**（`__cplusplus == 201703L`）。

## 问题分级（Severity Tiers）

审查输出必须为每条发现标注唯一档位：

| 档位 | 标记 | 含义 | 处置 |
|------|------|------|------|
| **必须修改** | `[MUST]` | 正确性 bug、UB、资源泄漏、数据竞争、违反硬性规范（AGENTS.md 强制项）、设计契约被破坏 | 打回编码，修复后才能合并 |
| **警告** | `[WARN]` | 潜在缺陷、异常安全瑕疵、可移植性风险、性能隐患（非热路径分配等）、规范灰色地带 | 强烈建议修复；允许带理由豁免 |
| **建议** | `[SUGGEST]` | 风格、可读性、可维护性改进；Modern C++ 简化机会 | 择优采纳，不阻塞合并 |

## 审查维度与检查清单

### A. 资源与所有权（Effective 13-16, 18-19；Core: R.1-R.13）

- [A1] 资源（锁/句柄/文件/内存）一律 RAII 包装（`std::scoped_lock`/`unique_ptr`/自定义 guard）——裸获取后手工释放 = `[MUST]`
- [A2] 裸 `new`/`delete`：热路径 = `[MUST]`；其余 = `[WARN]`（应有注释说明为何无法 RAII）
- [A3] 工厂函数返回 `unique_ptr`（Effective 18）而非裸指针 = `[MUST]`
- [A4] 资源所有权转移是否显式（move 语义、`std::move` 正确使用）——漏 move 造成拷贝 = `[WARN]`
- [A5] 异常路径下资源是否释放（构造失败时已构造成员自动析构？手动清理 = `[MUST]`）
- [A6] 用 `shared_ptr` 表达共享所有权、`weak_ptr` 打破环/观察——滥用 `shared_ptr` = `[SUGGEST]`；环 = `[WARN]`

### B. 类型安全与转换（Modern 5-8, 41；Core: ES.48-ES.58）

- [B1] C 风格 cast（`(T)x`）= `[MUST]`（用 `static_cast`/`reinterpret_cast`+注释）
- [B2] `reinterpret_cast` 无理由/无注释 = `[WARN]`
- [B3] `int` 枚举 + `static_cast` 模拟类型安全 = `[MUST]`（项目原则：类型安全第一，用 `enum class`/`std::variant`）
- [B4] 整数隐式窄化/符号混用（`size_t` ↔ `int`、无符号比较） = `[WARN]`；可致越界 = `[MUST]`
- [B5] 不必要的 `std::move` 抑制 NRVO/拷贝消除 = `[WARN]`（Modern 25: 返回值不要 `std::move`）
- [B6] 哨兵值（-1/nullptr 表示"无值"）应用 `std::optional` = `[SUGGEST]`；导致歧义 bug = `[MUST]`
- [B7] `std::variant`/`std::visit` 替代 `switch+cast` 异构分发（Modern 17）——项目核心原则 = `[WARN]` 起

### C. const 正确性（Effective 3；Core: Con.1-Con.4）

- [C1] 成员函数能 const 未 const = `[WARN]`
- [C2] 参数应 `const&` 却按值传大对象 = `[WARN]`；按值传仅移动类型（`unique_ptr` 等）= `[WARN]`
- [C3] 返回内部 `string_view`/引用而对象可能先亡 = `[MUST]`（悬垂）
- [C4] `mutable` 滥用（非缓存/非统计） = `[WARN]`
- [C5] 顶层 const 缺失（`const T*` vs `T*`）导致误改 = `[WARN]`

### D. 异常安全（Effective 29；Core: E.1-E.19）

- [D1] 函数未声明异常保证且无注释 = `[SUGGEST]`；基本保证不满足 = `[WARN]`
- [D2] 析构函数/`swap`/move 可抛异常（无 `noexcept`）= `[MUST]`
- [D3] 资源获取与初始化分离（构造中手工 try/catch 清理）= `[WARN]`（应 RAII 成员）
- [D4] 锁保护范围过大/跨函数释放 = `[WARN]`；锁内调用用户回调（可抛）= `[WARN]`（需注释契约）
- [D5] 异常穿过 `noexcept` 边界 = `[MUST]`

### E. 类设计（Effective 6, 7, 15, 20-26；Core: C.1-C.32）

- [E1] 多态基类无虚析构 = `[MUST]`（Effective 7）
- [E2] 需要虚析构却定义非虚 = `[MUST]`；虚析构 + 非多态 = `[WARN]`
- [E3] Rule of Zero/Three/Five 被违反（自定义析构却默认拷贝等）= `[MUST]` 或 `[WARN]`（视后果）
- [E4] 隐式转换构造函数缺 `explicit` = `[WARN]`（Effective 15）
- [E5] 深继承（>2 层）/菱形 = `[SUGGEST]`；组合优于继承（Effective 22）= `[SUGGEST]`
- [E6] 公开数据成员（非 POD 聚合）= `[WARN]`（破坏封装）
- [E7] 接口类只有纯虚 + 虚析构（Effective 31 最小接口）= `[SUGGEST]`

### F. 模板与现代特性（Modern 1-4, 12, 17, 23-24；Core: T.1-T.65）

- [F1] 模板参数约束缺失（应 `static_assert`/`if constexpr` 模拟 concepts）= `[WARN]`
- [F2] `std::string_view` 参数返回后悬垂（指向调用者临时）= `[MUST]`；只读参数用 `const std::string&` 造成隐式分配 = `[SUGGEST]`
- [F3] 时间用 `int` 毫秒而非 `std::chrono` = `[WARN]`（项目强制 `std::chrono`）
- [F4] 变参/折叠表达式滥用致可读性下降 = `[SUGGEST]`
- [F5] 头文件模板实例化膨胀/不必要 include = `[SUGGEST]`
- [F6] `auto` 推导类型使 API 意图不明（返回值） = `[SUGGEST]`

### G. 并发与线程（Effective 37, 39-40；Core: CP.1-CP.50）

- [G1] 共享可变数据无锁/无原子 = `[MUST]`（数据竞争 = UB）
- [G2] 锁粒度/死锁风险（嵌套锁、锁顺序不一致）= `[MUST]` 或 `[WARN]`
- [G3] `weak_ptr::lock()` 后生命周期假设（lock 后对象可能亡）= `[MUST]`（需要整个临界区持 shared_ptr）
- [G4] 条件变量无 predicate loop（`while` 而非 `if`）= `[MUST]`（丢失唤醒）
- [G5] 线程亲和性/归属线程契约未声明 = `[WARN]`；违反契约 = `[MUST]`
- [G6] 原子类型误用（`std::atomic<shared_ptr>` 在 C++17 有锁）= `[WARN]`（项目已知：04 §6.5 P2-7）
- [G7] 跨线程传递引用/指针无同步 = `[MUST]`

### H. 性能（Effective 29-30, 41-42；Core: Per.1-Per.24）

- [H1] 热路径（鼠标移动/定时器/分发）堆分配 = `[MUST]`（项目原则：热路径零堆分配）
- [H2] 热路径虚函数/锁（可避免时）= `[WARN]`（需注释说明为何必要）
- [H3] 不必要的拷贝（按值传大对象、返回值拷贝）= `[WARN]`；`std::function` 在热路径创建 = `[WARN]`
- [H4] 容器预留缺失导致热路径重分配 = `[SUGGEST]`
- [H5] 重复构造/查找（可缓存）= `[SUGGEST]`

### I. 命名与风格（项目强制，来自 AGENTS.md/style-guide.md）

- [I1] 类/结构体无 `Ct` 前缀（`crisp` 命名空间外类型例外）= `[MUST]`
- [I2] 成员变量无 `m_`/静态无 `s_` 前缀 = `[WARN]`
- [I3] 函数非 `lowerCamelCase`/常量非 `k` 前缀 = `[WARN]`
- [I4] 函数体 > 60 行（硬上限）= `[WARN]`（超限必须拆分或注明理由）
- [I5] 多职责函数（SRP 违反）= `[WARN]`；可测试性受损 = `[WARN]`
- [I6] 公开 API 缺英文 Doxygen 注释（`///`）= `[WARN]`；并发/所有权敏感函数无注释 = `[WARN]`
- [I7] 头文件含非 inline 函数体（ODR 违规）= `[MUST]`
- [I8] 源码非 UTF-8 无 BOM / 内嵌非 ASCII 字面量 / 非 LF 行尾 = `[WARN]`
- [I9] `using namespace std;` 在头文件 = `[MUST]`；在 cpp = `[SUGGEST]`
- [I10] 全局可变状态 = `[WARN]`；影响可测试性 = `[WARN]`
- [I11] `#define` 模拟函数 = `[WARN]`（用 `constexpr`/`inline`）

### J. 可观测性（项目设计原则 #2；Core: 无直接对应）

- [J1] 事件轨迹（trace）是否为"事后打补丁"而非一等公民 = `[WARN]`
- [J2] 异步丢弃/拒绝路径是否有 trace 记录 = `[WARN]`（项目：队列 trace kDropped）
- [J3] trace 记录是否跨线程写（CtTrace 严格单线程）= `[MUST]`

### K. 测试质量（项目：手写 test double，禁 mock 框架）

- [K1] 公开 API 未覆盖/边界未测（空队列、溢出、跨线程、异常路径）= `[WARN]`
- [K2] 测试断言过弱（只验无崩溃不验行为）= `[WARN]`
- [K3] 测试有时序依赖/flaky = `[WARN]`（如依赖真实毫秒定时器，应可注入）
- [K4] 测试引入 mock 框架 = `[MUST]`（项目强制禁止）

## 审查输出模板

```
AUDIT VERDICT: PASS | CONDITIONAL | REJECT
(REJECT: 存在 [MUST] 项；CONDITIONAL: 仅 [WARN]/[SUGGEST])

## Findings（按档位分组，每条含 file:line）
### [MUST]（必须修改）
- file:line — 问题 — 违反条款/规范 — 建议修法
### [WARN]（警告）
- file:line — 问题 — 建议修法
### [SUGGEST]（建议）
- file:line — 问题 — 建议修法

## 覆盖范围
- 审查的文件清单 + 每个文件的检查维度

## Handoff
- REJECT → RETURNED_TO_CODER（附 findings）
- CONDITIONAL → 建议修复 [WARN] 后进入审查；可豁免项需列理由
- PASS → READY_FOR_CODE_REVIEW
```

## 参考

- 项目细则：`AGENTS.md`、`docs/style-guide.md`
- 规范原则：`.github/skills/cpp-coding-standards/SKILL.md`
- 现代特性：`.github/skills/cpp-modern-best-practices/SKILL.md`
- 条款原文：Scott Meyers《Effective C++》3rd /《Effective Modern C++》；isocpp.github.io/CppCoreGuidelines/
