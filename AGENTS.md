# CRISP 项目指南（AGENTS.md）

> 本文件是 **CRISP** 项目的强制性编码规范。所有 agent（Copilot 等）在为本项目写代码时必须遵守。
> 项目目标：用现代 C++（C++17）逐步替代 Qt 模块的自研 GUI/应用框架。

---

## 项目设计原则（Design Principles）

> 来源：`docs/event-system/03-rewrite-proposal.md` §1.2。任何设计决策不得违反以下原则。

1. **类型安全第一** — 事件类型在编译期可校验，杜绝 int 枚举 + static_cast。
2. **可观测性内置** — 事件轨迹（trace）是一等公民，不是事后打补丁。
3. **所有权明确** — 用 RAII / 智能指针表达所有权，杜绝手工 new/delete。
4. **线程规则显式** — 跨线程投递是"类型不同"的操作，而非运行时碰运气。
5. **热路径零堆分配** — 高频路径（鼠标移动、定时器）用值语义/对象池。
6. **无代码生成** — 不用 moc / qmake，纯标准 C++，构建用 CMake。

---

## 代码风格（C++）

### 文件组织（强制）
- **`.cpp` 与 `.h` 严格分离**，禁止在头文件中写函数实现体（除非是模板 / `inline` 必需）。
- 头文件仅声明；实现一律放 `.cpp`。
- 头文件必须包含 include guard（`#pragma once` 或传统 guard，二选一，全项目统一——**用 `#pragma once`**）。

### 命名规范（强制）
- **所有自定义类 / 结构体以 `Ct` 开头**（Crisp Type），例如 `CtEvent`、`CtDispatcher`、`CtQueue`。
- 命名空间统一为 `crisp`（及 `crisp::event` 等子命名空间）。
- 成员变量：`m_` 前缀（如 `m_timerId`）；静态成员用 `s_` 前缀。
- 函数 / 方法：`lowerCamelCase`（如 `dispatchEvent()`）。
- 常量 / 枚举值：`k` 前缀或全大写（如 `kMaxQueueSize`），全项目统一。
- 宏（尽量不用）：`CRISP_` 前缀。

### 函数质量（强制）
- **单一职责（SRP）**：每个函数只做一件事。超过一个职责必须拆分。
- **行数限制**：函数体 ≤ 60 行（含空行）。超过必须拆分。
- **重要函数必须生成注释**：对每个公开 API、复杂算法、有并发/所有权语义的函数，用英文 Doxygen 风格注释（`///` 或 `/** */`）。
- 所有注释使用**英文**。

### 文件编码与可移植性（强制）
- **源码文件一律 UTF-8（无 BOM）**。
- 禁止在源码中直接嵌入非 ASCII 字符（如中文字符串字面量）；需要时用 `u8"..."` 或转义。
- 行尾：LF（Unix）为主；如必须 CRLF 请用 gitattributes 统一。
- 路径处理：避免依赖特定平台分隔符，优先用 `std::filesystem`（注意：当前 MinGW 8.1 的 `<filesystem>` 有 bug，见下）或封装层。
- 禁止使用非标准扩展（`__attribute__`、MSVC-only 语法），除非包在宏里。

### 现代 C++ 使用（强制）
- **语言标准：C++17**（当前工具链 MinGW-w64 8.1.0 的限制，`__cplusplus == 201703L`）。
- 优先使用：`std::variant`、`std::optional`、`std::string_view`、`std::any`、智能指针、`std::chrono`（而非 int 毫秒）。
- **避免 `std::filesystem`**：MinGW 8.1 的 libstdc++ 中 `<filesystem>` 存在已知 bug（`fs_path.h` 缺 `operator!=`，GCC 9 才修复）。用 `Qt` 路径 API 或自研辅助层替代。
- 不用裸 `new`/`delete`；不用 C 风格 cast（用 `static_cast`/`reinterpret_cast` 并注释理由）。
- 用 RAII 管理资源（锁、文件、句柄）。

---

## 架构约定

- 模块边界清晰，遵循 `docs/` 下的设计文档。
- 事件系统：`crisp::event`，见 `docs/event-system/`。
- **逐步替代 Qt**：新模块不得依赖 moc / qmake；可依赖 Qt（嵌入期），但每一层都要可独立编译测试。
- 构建：CMake（≥ 3.20），单元测试用手写 test double（**禁止 mock 框架**）。

---

## 代码审计门禁（强制）

- **所有生成的代码（`src/`、`tests/`、`examples/`）必须先过代码审计子agent**（`.github/agents/code-auditor.agent.md`），审计依据 `.github/skills/cpp-review-rules/SKILL.md`（Effective C++ / Modern Effective C++ / C++ Core Guidelines / Google Style 综合清单）。
- **分档输出**：每条发现必须标注 `[MUST]`（必须修改）/ `[WARN]`（警告）/ `[SUGGEST]`（建议）。
  - 存在 `[MUST]` → **REJECT**，打回编码子agent 修复后才能进入代码审查。
  - 仅 `[WARN]`/`[SUGGEST]` → **CONDITIONAL/PASS**，进入代码审查子agent；`[WARN]` 应尽量修复或注明豁免理由。
- 审计与审查分离：审计按通用规范（Effective C++ 等）查质量，审查核对与设计文档的一致性；两者都是门卫，都拥有打回权。
- 审计/审查/测试循环：**编码 → 审计 → 修复 → 审查 → 修复 → 测试 → 修复**，直到 `DEPLOYED_OK`。

---

## 构建与测试

```powershell
# 配置（示例，具体见各子项目）
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
# 构建
cmake --build build --parallel 2   # 注意：MinGW 8.1 编译器是 32 位，并行度 >2 可能 OOM
# 测试
ctest --test-dir build
```

---

## 参考规范来源

- 现代 C++ 实践：见 `.github/skills/cpp-modern-best-practices/SKILL.md`
- Effective C++ / C++ 编码规范：见 `.github/skills/cpp-coding-standards/SKILL.md`
- **代码审查规则（Effective C++ / Modern Effective C++ / Core Guidelines / Google Style 综合 + 分档）：见 `.github/skills/cpp-review-rules/SKILL.md`**
- 本项目风格细则：见 `docs/style-guide.md`
