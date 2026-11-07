# UI3 O4 收尾：控件树级完整双跑（crisp::ui）

> 权威规范：`docs/ui/06-ui3-architecture.md` §17（O4：控件树级完整双跑——同一控件树双模式收到相同载荷序列；UI3 交付 mapper 级交叉断言，控件树级留 UI4 控件完备后）。本文档登记 O4 落地。

## 1. 交付范围（O4 ✅）

| 文件 | 内容 |
|------|------|
| `tests/ui/dual/ui_dual_model.h` | 共享模型：控件树（label/button/lineedit + 焦点链）强持有 + 语义事件日志（C/E/F）+ `CtDualInput` 变体 |
| `tests/ui/dual/ui_dual_common.h` | 共享 `parseLogPath`（消除两后端重复） |
| `tests/ui/dual/ui_dual_driver.cpp` | `buildTree` + `drivePayloads`（childAt 命中 → 坐标翻译 → dispatchInput；kTab 步进焦点链；key 仅 press 路由） |
| `tests/ui/dual/dual_main_win32.cpp` | Win32 后端：合成 `CtRawMsg` → `mapWin32Key/Mouse` → 核心载荷 |
| `tests/ui/dual/dual_main_qt.cpp` | Qt-host 后端：Qt 字段桥载荷 → `mapBridgeKey/Mouse` → 核心载荷 |
| `tests/ui/compare_ui_dual.cmake` | 字节比对 + 空文件 FATAL（防假绿） |
| `src/ui/ct_widget_base.h` | `localHitTest` 新增（widget-local 命中；`hitTest` 保持 parent-local 不动） |
| `src/ui/ct_push_button.cpp` | `onMouseInput` 改用 `localHitTest`（载荷 widget-local 契约） |

## 2. 语义序列与排除契约

**固定语义序列**（两后端完全相同）：
1. `focusChain()->setFocus(edit)` → 焦点进入编辑框（F）
2. 鼠标 press+release 于按钮 (50,55)（root-local）→ 按钮点击完成（C 40 15，widget-local 翻译后）
3. Tab ×2 → 焦点步进 edit→button→edit（F ×3）

**日志格式**（MUST-3/D12/WARN-2 排除契约如实落地）：
- `C x y`：点击（widget-local 坐标，无时间字段）
- `E <len>`：文本变更（事件种类 + **长度**——text 字节在 Win32 映射器按 MUST-3 留空、Qt 桥映射器携带，故只比长度）
- `F <objectId>`：焦点进入（objectId 由 CtReceiver 原子计数器分配，两后端同序）

timestamp 完全不进日志（Win32 steady_clock 推导 vs Qt 桥透传——WARN-2/D6 排除）。

## 3. 关键契约（O4 定案）

### 3.1 坐标语义（07 §3 的落地）

- `childAt(px,py)` 用 **parent-local** 几何（root 直接子级 == root-local）。
- payload 是 **widget-local**（crisp::input 契约）——`drivePayloads` 命中后按 `target.geometry()` 平移，再 `dispatchInput`。
- `CtWidgetBase::hitTest` 保持 **parent-local**（UI1 测试锁定）；新增 `localHitTest` 供输入钩子测 widget-local 点（按钮/编辑框）。

### 3.2 焦点路由

- 非 Tab 的 key 只路由给 `chain->current()`（**仅 press**，release 不步进/不派发——对齐源层投递）。
- `kTab` → `chain->next(kNext)`（步进 edit→button→edit）。
- 链配置错误（无 current）→ **assert 暴露**（不静默回退）。

### 3.3 RAII 与所有权

- 模型**强持有** root/button/edit + 语义 handler tokens（`CtHandlerToken` 析构会注销——§7.7，必须保持存活）。
- 树只存弱引用（P5）；label 不强持有（不参与命中，随树作用域析构）。

### 3.4 假绿防护（审计 WARN-2/3 定案）

- 后端写日志前 `std::remove` 旧文件（增量构建不留残留）；打开失败 → stderr + 退出非零。
- 比较脚本对**空文件** FATAL_ERROR（空-vs-空字节匹配 = 假绿）。

## 4. 测试现状

ctest **12/12 全绿**，新增：
- `dual_run_ui_win32` / `dual_run_ui_qt`（WIN32 / Qt6 条件编译）
- `dual_ui_compare`（`cmake -P` 字节比对，DEPENDS 两后端）

两后端日志字节一致：
```
F 4
C 40 15
F 4
F 3
F 3
F 4
```

## 5. 关联

- UI3 交付的 mapper 级交叉断言（`test_ct_input_mapper_qt.cpp`）与本次控件树级双跑构成完整的两级验证。
- 后续（UI6/MT4）可选；UI 子系统主线（M1-M5 + 双跑）已闭环。
