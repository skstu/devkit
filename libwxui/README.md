# libwxui

基于 wxWidgets 的 C++20 XML 桌面 UI 库。控件是由 `UIManager` 管理的逻辑对象，
大部分由库自行绘制；输入框等使用原生子窗口。应用通过 `DesktopWindow` 与事件回调
组织界面，保持本地窗口实现封装在库内。

## 独立构建

`CMakeLists.txt` 导出静态 target `libwxui`，自行发现 wxWidgets（core/base/xml/aui/html/stc）、
fmt、RapidJSON、utfcpp。可通过 `add_subdirectory` 集成，不需要原宿主私有变量。
完整集成示例见 [TdDesk](../../projects/client/tddesk/README.md)。

## TdDesk 推动的通用能力

- `CreateControlFromXml(xml)` 创建独立控件片段，嵌套未知标签抛出异常；不会覆盖已
  注册的自定义工厂。应用可把片段插入已有树。
- `HorizontalLayout` / `VerticalLayout` 支持 `weight` 正整数分配伸缩空间，并计算
  子控件 padding 边距、childpadding；默认权重为 1。
- `bkcolor` + `bkcolor2` 绘制垂直渐变；`gradientend="0.5"` 在半高处达到第二色，
  后半保持该色。`cornerradius="2,2,0,0"` 为左上、右上、右下、左下圆角。
- `Label` / `Button` 支持 `fontsize`（逻辑像素）、`fontface`、`bold`。
  `wordwrap="true"` 自动换行；`Label::MeasureText(width)` 返回包括 textpadding 的高度。
  字体及按文字 / 宽度 / 字体生成的换行结果缓存；普通 Label 的默认行为保持不换行。
- `DecimalLabel` 按字符串绘制前段、倒数第二 / 三位大号字、末位上标，用于定点价格，
  不将文本转换为浮点值。
- `Icon` 使用矢量路径绘制 circle、unavailable、info、add、close、chart、candlestick、
  launch、star、star-outline、lock；继承 Button 的状态与 click 回调。
- `PricePlot` 提供当前快照与 OHLC 日 K / 收盘折线；实际数值只用于坐标，标签仍保留
  输入字符串。鼠标移动发送 `pointchange`，param1 为 bar 索引。库不生成行情或历史。
- `List::BeginUpdate/EndUpdate` 允许嵌套批量操作；`SetVirtualItems(count, height, factory, scroll)`
  只物化可见行及两側各一行。factory 在 UI 线程运行，返回 `ListContainerElement`；
  被移出的行及原生子控件释放。factory 应从外部模型取得最新数据；应用如需索引，
  用弱引用，避免永久保留已移出屏幕的对象。当前使用固定虚拟行高，支持逻辑选择索引。
- `List` 的 `stripeheight` / `stripecolor` 绘制包括空白区域在内的表格条纹。
  可选 `columnlinecolor` 按表头边界延伸列线到文本行和空白区；未设置时保持原行为。
- `UIManager::Snapshot(area)` 在 UI 线程按逻辑 1x 绘制自绘控件树，区域需位于客户区内。
  用于离线视觉对照，不含原生子窗口内容或系统边框，不替代真实屏幕截图。
- `DesktopWindowSpec.standardMenu` 可选标准 File 菜单；默认不附加。Cmd/Ctrl+Q 始终
  进入窗口关闭流程。`ShowDialog` 使用 XML 和逻辑控件树构造模态窗口，约定 close /
  accept 按钮，返回是否接受。
- EventSink 调用前复制回调，允许回调重建包含自身的控件树。`Poster()` 在关闭 / 销毁
  后取消队列发送。业务仍需先停止后台生产者，再销毁窗口。

```xml
<VerticalLayout childpadding="4">
  <HorizontalLayout height="34" bkcolor="#d8dddd" bkcolor2="#aeb4b4">
    <Button text="产品" width="76" height="28" padding="0,6,0,0"
            cornerradius="2,2,0,0" fontsize="14" align="center"/>
    <Control/>
  </HorizontalLayout>
  <Label wordwrap="true" textpadding="5,3,5,3" text="来源状态与完整通知"/>
  <DecimalLabel height="45" text="1.13298"/>
</VerticalLayout>
```

## 绘制与资源边界

UIManager 按更新区域裁剪；macOS 复用原生 DC 的 graphics context，其他后端每次
绘制取得一个共享画布。渐变、圆角及图标借用同一画布并保存 / 恢复状态。相同文字
或颜色不重复触发刷新。

虚拟列表减少控件数，仍需调用方控制模型、队列、日志和历史规模。字体缓存上限为
每个 Label 8 种变体，换行缓存仅保留当前结果。物化控件操作只能在 UI 线程进行。

2026-10-05 检查通过的集成回归位于 `tests/tdbrg/tddesk` 和 `tests/tdbrg/wxui`；原
`3rdparty/libwxui/tests` 中依赖旧宿主 assets.hpp 的测试未直接迁移或执行。当前不能
声称完成整个库审计、跨平台绘制一致性或自绘控件的可访问性验收。
