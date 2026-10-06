# CRISP

> 用现代 C++（C++17）逐步替代 Qt 模块的自研 GUI/应用框架。
> 当前阶段：**事件系统**（M1 同步核心 + M2 异步核心 已完成）。

---

## 1. 项目简介

CRISP 的目标是用**纯标准 C++17** 逐步替代 Qt 的各个模块，摆脱 moc/qmake 等代码生成工具链，同时保留 Qt 的成熟设计思想。当前实现的是第一个子系统——**事件系统**（`crisp::event`），它替代 Qt 的 `QCoreApplication` 事件分发部分。

设计文档：`docs/event-system/`（01 调研 → 02 痛点 → 03 重写方案 → 04 重设计规范）。

## 2. 环境要求

| 工具 | 版本 | 说明 |
|------|------|------|
| 编译器 | MinGW-w64 **8.1.0**（32 位） | 工具链限制见 AGENTS.md；`__cplusplus == 201703L` |
| CMake | ≥ 3.20 | 构建系统 |
| C++ 标准 | C++17 | 全局强制 |
| Qt（可选） | 6.2.4（`D:\Qt\6.2.4`） | 仅 M3 Qt 桥需要；M1/M2 不依赖 Qt |

> ⚠️ **PATH 陷阱**：本机 PATH 中可能有多个 g++（如 Strawberry 4.9.2 太旧不支持 C++17）。务必让 CMake 使用 `C:\mingw64\bin\c++.exe`。

## 3. 编译与运行

### 3.1 首次配置

```powershell
cd D:\crisp

# 方式 A：PATH 中已是正确的 MinGW 8.1
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 方式 B：显式指定编译器（推荐，避免 PATH 干扰）
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_CXX_COMPILER=C:/mingw64/bin/c++.exe
```

### 3.2 编译

```powershell
cmake --build build --parallel 2
```

> `--parallel 2`：MinGW 8.1 是 32 位编译器，并行度 >2 可能 OOM（AGENTS.md 强制）。

构建产物：
- `build/src/event/libcrisp_event.a` — 事件库（静态）
- `build/tests/event/event_tests.exe` — 单元测试
- `build/examples/event_demo/event_demo.exe` — 示例程序

### 3.3 运行单元测试

```powershell
ctest --test-dir build --output-on-failure
# 或直接运行
.\build\tests\event\event_tests.exe
```

当前 36 个用例全部通过（M1 §13.1/§13.2 + M2 §13.3）。

### 3.4 运行示例

```powershell
.\build\examples\event_demo\event_demo.exe
```

示例演示完整链路：
- **M1 同步**：`send` 分发、过滤器拦截、观察者 pre/post、trace 记录
- **M2 异步**：`post` 投递、`CtEventLoop` 排空、`CtTimerEventSource` 定时注入

### 3.5 干净重建（可选）

```powershell
cmake --build build --parallel 2 --clean-first
```

## 4. 库设计原理与目的

### 4.1 为什么重写 Qt 事件系统

Qt 事件系统（`QEvent` + `QCoreApplication::notify`）的痛点（详见 `docs/event-system/02`）：

1. **伪类型系统**：`QEvent::Type` 是 int 枚举，分发靠 `static_cast` + 运行时 switch，类型错误运行期才暴露。
2. **调试困难**：事件流转是黑盒，无内置轨迹。
3. **所有权手工管理**：`deleteLater()` 延迟删除、事件所有权转移靠约定。
4. **notify 上帝函数**：全局分发逻辑集中，难以扩展与测试。

### 4.2 设计六原则（AGENTS.md 强制）

| 原则 | 落地 |
|------|------|
| **类型安全第一** | `CtEvent<T>` 强类型；`CtEventSet<Ts...>` 闭合类型域；`static_assert` 编译期三重防线，杜绝"再 cast" |
| **可观测性内置** | `CtTrace` 无锁环记录 6 相位（dispatch/filtered/delivered/observed/queued/dropped），trace 是一等公民 |
| **所有权明确** | `CtReceiver` 必须 `shared_ptr` 持有；dispatcher 表存 `weak_ptr`；`CtHandlerToken` RAII 注销 |
| **线程规则显式** | `CtThreadAffinity` 记录创建线程 + 断言；`post` 路由到接收者归属线程，不做运行时碰运气 |
| **热路径零堆分配** | `std::variant` 值语义存储；COW 快照；测试用 `operator new` 计数器验证 send/broadcast 零分配 |
| **无代码生成** | 纯 C++17 + CMake，无 moc/qmake；`CRISP_EVENT_TYPE` 宏只是普通预处理器展开 |

### 4.3 核心概念映射（Qt → CRISP）

| Qt 部件 | CRISP 替代 | 现代 C++ 特性 |
|---------|-----------|----------------|
| `QEvent` + `QEvent::Type` int 枚举 | `CtEvent<T>` | 类模板 + `CtEventTraits<T>` 编译期特化 |
| `QObject::event()` / `notify()` 上帝函数 | `CtDispatcher<EventSet>` | `std::variant` + `std::visit` 穷尽匹配 |
| 事件过滤器 | `CtFilter<T>` | `std::function` 类型擦除 |
| 信号槽连接 | `CtHandler<T>` + `CtHandlerToken` | `std::shared_ptr`/`weak_ptr` + RAII |
| `QCoreApplication::postEvent/sendEvent` | `post/postTo/send/broadcast` | 归属显式化 + 擦除边界 |
| `QThread::exec()` 每线程事件循环 | `CtEventLoop` + `CtQueue` | `std::condition_variable` + `thread_local` |
| `QAbstractEventDispatcher` / `QTimer` | `CtEventSource` / `CtTimerEventSource` | 模板化源（方案 A） |
| Qt 黑盒调试 | `CtTrace` | 无锁 SPSC 环形缓冲（`std::atomic` 内存序） |

### 4.4 分层架构

```
应用层      （业务代码：CtReceiver 子类 + 业务事件）
   ↓ 只依赖核心层
核心层      （crisp::event：纯 C++17，可独立编译测试）
   ↓ 只经 CtEventLoop
平台层      （CtEventSource：注入 CtQueue，绝不接触接收者/分发器）
```

关键规则：
- 核心层**绝不依赖**应用层/平台层。
- `CtEventLoop` 是连接队列→分发器的**唯一**场所。
- `CtEventSource` 只向 `CtQueue` 注入事件。

### 4.5 演进里程碑

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| M0 | 架构 + 接口规范定稿（`04-event-system-redesign.md`） | ✅ |
| M1 | 同步核心：强类型事件、COW 分发器、无锁 trace | ✅ 36 测试全绿 |
| M2 | 异步核心：队列、事件循环、事件源、post/postTo | ✅ 36 测试全绿 |
| M3 | Qt 桥（可选）：`QtEventBridge` 适配器 + 双跑验证 | ⬜ 进行中 |
| M4 | 加固：对象池、flat-map、`CtOpaqueEvent` 逃生舱 | ⬜ 未开始 |

## 5. 目录结构

```
crisp/
├── AGENTS.md            # 项目编码规范（强制）
├── CMakeLists.txt       # 顶层构建
├── docs/
│   ├── style-guide.md   # 风格细则
│   ├── qt6-xml-ui-and-event-system.md  # Qt 调研
│   └── event-system/    # 事件系统设计文档（01-04）
├── src/
│   └── event/           # crisp::event 库源码
├── tests/
│   └── event/           # 单元测试（手写 test double，无 mock）
└── examples/
    └── event_demo/      # 演示程序
```

## 6. 项目规则速览（详见 AGENTS.md）

- 类/结构体 `Ct` 前缀；枚举值 `k` 前缀；成员 `m_` 前缀；方法 `lowerCamelCase()`。
- 源码 UTF-8 无 BOM、LF 行尾、**禁止非 ASCII 字符**。
- 禁用 `std::filesystem`（MinGW 8.1 的 libstdc++ bug）。
- 禁止 mock 框架；测试用手写 test double。
- 提交消息：`feat:/fix:/refactor:/docs:/test:/build:/chore:/perf:/revert:` 前缀，纯 ASCII。
