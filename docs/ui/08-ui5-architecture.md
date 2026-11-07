# UI5 样式系统（crisp::ui）架构规范

> 权威规范：`docs/ui/03-rewrite-proposal.md` §4.5（CtStyle 纯接口 + CtBasicStyle 三原色集 + 绘制委托契约）。本文件记录 UI5 落地的裁决与偏差登记，并作为 UI5 的实现依据。

## 1. 交付范围（UI5 ✅）

| 模块 | 内容 | 状态 |
|------|------|------|
| `CtButtonState` | 共享按钮状态枚举（`ct_button_state.h`，独立头） | ✅ |
| `CtStyle` | 纯虚接口：textColor/backgroundColor/borderColor/borderWidth/textColorForState/sizeHint | ✅ |
| `CtBasicStyle` | 编译期调色板 `kDefaultPalette` + 确定性状态映射 + `defaultStyle()` | ✅ |
| 控件绘制委托 | `CtPushButton`/`CtLineEdit`/`CtLabel::paint(CtPainter&, rect)` 只问 style，不嵌样式逻辑 | ✅ |
| sizeHint 联动 | 三控件 sizeHint = 后端 fontMetrics + style frame（padding+border） | ✅ |
| 共享常量 | `kFixedCharWidthPx`/`kFixedLineHeightPx`（PixelBuffer 后端 + painter + caret 共用） | ✅ |

## 2. 与 03 §4.5 草案的偏差登记

| # | 草案 | 落地 | 裁决 |
|---|------|------|------|
| 1 | `textColorForState(w, CtPushButton::CtButtonState)` | `CtButtonState` 提为独立头 `ct_button_state.h`；`CtStyle` 只引用状态类型，不 include 控件头 | **合理演进**（WARN-6：纯接口不耦合具体控件；`CtPushButton::CtButtonState` 为 using 别名） |
| 2 | `CtStyle::sizeHint(w)` 语义未细化 | frame = 2×padding + 2×border（边框由 paint 画在 rect 内，hint 必须预留） | **补充定案**（WARN-1） |
| 3 | 控件 paint 自建 painter（UI4 存根） | `paint(CtPainter&, rect)`：painter 由 owner（backing store）持有并 begin/end；控件只 record | **合理演进**（WARN-2：避免调用栈上 0.5MB 命令缓冲） |
| 4 | UI4 `kTextWidgetPaddingPx` 固定 padding | 删除；改经 `CtStyle::sizeHint` | **演进** |
| 5 | `CtLabel::paint` 画在 rect 原点 | 尺寸 hint 含 frame，但 label 无背景框，文本从 rect 原点绘制（不内缩） | **登记偏差**（P2：首版可接受；若未来加背景需退 frame.width/2） |

## 3. 关键契约（UI5 定案）

### 3.1 CtStyle 纯接口

- 控件**只问 style**（`effectiveStyle()`：`m_style ? *m_style : defaultStyle()`）；样式逻辑不在控件内（P3）。
- 无运行期 CSS/解析器（P2）：调色板为编译期常量；扩展走"追加虚方法"而非配置化（开放问题 #7）。
- `defaultStyle()`：程序生命期 static（const，跨线程安全）；控件不拥有 style（调用方保持 alive；nullptr = 回退默认）。

### 3.2 sizeHint 语义

- 控件 sizeHint = `backend.fontMetrics(text)` + `style.sizeHint(w)`。
- `CtBasicStyle::sizeHint` = `2×horizontalPadding + 2×borderWidth`（横向）与 `2×verticalPadding + 2×borderWidth`（纵向）——边框画在 rect 内，hint 必须预留（WARN-1）。

### 3.3 paint 委托

- `paint(CtPainter&, rect)`：painter 由 owner 提供（backing store/窗口 paint pass），**已 begin**；控件只 record（fillRect/drawText），不 begin/end（WARN-2）。
- 按钮/编辑框：背景填充 + 四边边框 + 居中/偏移文本（按钮居中，编辑框退 frame.width/2）+ caret（聚焦时，列 = `textX + cursor × kFixedCharWidthPx`）。
- 标签：文本画在 rect 原点（透明无框，偏差 #5）。

### 3.4 共享常量

- `kFixedCharWidthPx = 8` / `kFixedLineHeightPx = 16`（ct_paint_types.h）：PixelBuffer 后端 drawText、painter 记录 clip/damage、LineEdit caret 列计算共用（WARN-4/P1-2）——不可漂移。

### 3.5 线程与生命周期

- `setStyle(const CtStyle*)`：`assertSameThread`（debug abort / release no-op）；style 生命周期必须长于 widget（caller 契约，WARN-7）；nullptr = 回退默认。

## 4. 测试现状

- `tests/ui/test_ct_style_basic.cpp` — 调色板确定性表驱动、style frame sizeHint、**同控件不同 style 出不同像素**（PixelBuffer 对比，P3 契约）、LineEdit caret 聚焦/失焦、Label 文本色。
- `tests/ui/test_ct_line_edit.cpp` — caret 鼠标定位含 frame 偏移（点击 padding 内 → col 0；点击文本内 → 正确列；点击超尾 → 钳制到文本末尾）。
- 既有 UI4 测试同步更新（sizeHint 含 frame 的黄金值）。

**ctest：9/9 全绿**（ui_tests 含 UI4 24 例 + UI5 7 例新增）。

## 5. 后续

- **控件树级完整双跑**（UI3 O4）：Win32 vs Qt 宿主同一控件树载荷序列对比——UI4/UI5 控件与绘制已完备，现在可做。
- **UI6**（可选）：扩展控件（组合框/复选框/滚动条）+ 声明式 UI 工具。
- 声明式 UI（UI5 原路线图项）改入 UI6 范围。
