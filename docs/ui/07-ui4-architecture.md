# UI4 基础控件 + 布局 + 焦点链（crisp::ui）架构规范

> 权威规范：`docs/ui/03-rewrite-proposal.md` §4.4（接口草案 + 关键契约）。本文件记录 UI4 落地时的**裁决与偏差登记**（对齐 04/05/06 的登记惯例），并作为 UI4 的实现依据。

## 1. 交付范围（UI4 ✅）

| 模块 | 内容 | 状态 |
|------|------|------|
| `CtLabel` | 纯文本标签：`text()/setText(text)/sizeHint(backend)`，setText 触发 markDamage | ✅ |
| `CtPushButton` | 显式状态机（kNormal/kHovered/kPressed/kDisabled）：`state()/setText()/onClick()` | ✅ |
| `CtLineEdit` | 单行编辑器：`text()/setText()/cursorPos()/setCursorPos()/onTextChanged()`，按键/鼠标编辑纯逻辑 | ✅ |
| `CtBoxLayout` | 两遍布局：`addWidget()/addLayout()/relayout(bounds, backend)/sizeHint(backend)` | ✅ |
| `CtFocusChain` | 显式焦点链：`add()/setFocus()/next(dir)/current()`，kFocusIn/kFocusOut 轨迹配对 | ✅ |
| 语义事件载荷 | `CtClickPayload`/`CtTextChangedPayload`/`CtFocusPayload`（`crisp::input`，CRISP_EVENT_TYPE） | ✅ |
| 语义分发器 | `CtSemanticEventSet` + 每线程 `CtDispatcher` 注册表（`ct_dispatcher_registry`） | ✅ |

## 2. 与 03 §4.4 草案的偏差登记

| # | 草案 | 落地 | 裁决 |
|---|------|------|------|
| 1 | `onClick/onTextChanged` 无返回值 | 返回 `CtHandlerToken`（RAII 注销） | **合理超集**（04 §7.7 token 契约；草案未禁止） |
| 2 | `setText` "triggers markDamage + **relayout**" | setText 只 `markDamage(geometry())`；**relayout 由 owner 显式调用**（CtBoxLayout 无 invalidate 递归，P7 对策） | **登记偏差**：owner 在 setText 后主动 `relayout()`；控件自身不触发 |
| 3 | `relayout(const CtRect&)` / `sizeHint() const` | 增加 `CtPaintBackend* backend` 参数（sizeHint 需后端字体度量，§4.4.1 明示） | **合理演进**（sizeHint 的度量来源只能是后端） |
| 4 | 显式链表 next/prev O(1) | `std::vector<weak_ptr>` + 索引步进（`(idx+step)%n` O(1)） | **合理替代**（复杂度契约满足；头注释已声明） |
| 5 | — | `distributeShare` 余数补偿（qGeomCalc 模型）+ 全 0 stretch 时均分 | **补充定案**（§4.4.3 未覆盖 stretch=0 的去向；本文件 §3.3 定案） |

## 3. 关键契约（UI4 定案）

### 3.1 输入钩子（onMouseInput/onKeyInput）

- `CtWidgetBase::dispatchInput`（禁用态 gate 后）把载荷转发给 **protected virtual** `onMouseInput/onKeyInput`；子类 override 保持 protected（外部只能经 `dispatchInput` 进入，P2-3 裁决）。
- 钩子不 accept 输入事件（raw 输入由应用经 UI3 `CtUiEventSet` 队列消费；语义事件是控件层产物）。
- 线程守门：`dispatchInput`/`setText`/`setEnabled`/`setCursorPos`/`setFocusFlag`/`setFocusable` 入口 `assertSameThread()`（04 §4.2）。

### 3.2 语义事件

- `CtSemanticEventSet = CtEventSet<CtClickPayload, CtTextChangedPayload, CtFocusPayload>`（只含语义载荷，不含 raw input — 与 UI3 `CtUiEventSet` 分离）。
- 每线程一个 `CtDispatcher<CtSemanticEventSet>`（注册表）；`CtWidgetBase` 缓存 dispatcher（首次使用填充，WARN-4 定案）。
- `emitSemantic<T>(payload)` 同步 `send` 到本控件 handlers（不广播）；`bind*Handler` 返回 RAII token。

### 3.3 布局两遍算法

- Pass 1 measure：每项 `sizeHint(backend)`（控件虚函数；文本控件用后端字体度量）。
- Pass 2 place：`setGeometry`。
- **stretch 分配（P1-3/P1-4 定案）**：share = `remainingExtra * itemStretch / remainingStretchTotal`（整数向下取整，**余数进位到下一项** — 仿 qGeomCalc）；当所有 stretch=0 时，extra **均分**给存活项（不留下尾部浮动空隙）。
- 线程契约：布局持有 `CtThreadAffinity`（创建线程），add/relayout/sizeHint 入口断言。
- `totalHint` 用 `std::int64_t` 累加（防 hint 超大回绕，P1-2）。
- 死控件（weak 失效）在 measure 得零 hint、place 跳过。

### 3.4 焦点链

- 存储：`std::vector<weak_ptr>` + `m_current` 索引 + `m_hasCurrent` 标志（区分"index 0 有焦点"与"无焦点"）。
- `setFocus(w)`：**只聚焦 focusable 控件**（P1-5 定案）；未注册 → no-op；已聚焦 → no-op。
- `next(dir)`：Tab 从当前索引 +1 环绕；Backtab −1 环绕；无焦点时 kNext 从链尾起（首项 a）、kPrevious 从链首起（末项）。
- `pruneDead()`：剪除死引用后**按 owner 身份重定位当前焦点项**（防索引越界 + 保持焦点位置，P1-1 定案）。
- 焦点流转：`setFocusFlag` 写 kFocusIn/kFocusOut 轨迹 + `emitSemantic<CtFocusPayload>`（gained/lost 均发，targetObjectId = 事件控件）。
- 线程契约：链持有 `CtThreadAffinity`，add/setFocus/next 入口断言。

## 4. 测试现状

- `tests/ui/test_ct_push_button.cpp` — 状态机全路径（按下/释放/取消/禁用/悬停）、onClick 语义事件、setText+damage。
- `tests/ui/test_ct_line_edit.cpp` — 编辑全路径（键入/退格/删除/光标/鼠标定位）、onTextChanged、非打印键 no-op、setText 钳制。
- `tests/ui/test_ct_label.cpp` — 文本存储、setText+damage、sizeHint（PixelBuffer 8x16 固定度量）。
- `tests/ui/test_ct_box_layout.cpp` — 黄金手算（stretch 余数补偿、嵌套、死控件、sizeHint 聚合、无缝隙断言 `right()==容器宽`）。
- `tests/ui/test_ct_focus_chain.cpp` — add/dedup、setFocus 轨迹配对、Tab/Backtab 环绕、死控件剪枝、未注册 no-op。

**ctest：9/9 全绿**（ui_tests 含 UI4 24 例新增）。

## 5. 后续（UI5）

- `CtStyle` 纯接口 + `CtBasicStyle` 三原色集（替换 UI4 固定调色板与 `kTextWidgetPaddingPx`）。
- 控件绘制（paint 当前为存根 — SUGGEST 记录：UI4 测试锁定状态机/文本/布局；绘制行为随 UI5 样式接入 + PixelBuffer 逐像素断言）。
- 控件树级完整双跑（Win32 vs Qt 宿主，UI3 O4 开放问题）。
