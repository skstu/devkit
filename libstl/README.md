# libstl

C++20 标准库辅助工具。保留 `stl::*`、`ProcessCache`、`ProcessObjectCache` 和旧头文件入口，现有调用方可继续使用 `#include <libstl.hpp>`。

`src/file.cc`、`src/string.cc` 仅为历史注释占位，当前 CMake 不编译；实际定义
在 `utils.cc` / `libstl/detail/file.ipp` 和 `libstl/utils.hpp`。本轮保留两个占位文件，
避免破坏尚未核验的外部显式源文件清单；它们不是需要迁移的实现。

## 结构与接入

| 头文件 | 职责 | 依赖 |
| --- | --- | --- |
| `libstl/container.hpp` | 带锁容器 | STL |
| `libstl/cache.hpp` | 类型安全的对象缓存、值缓存 | STL，RTTI |
| `libstl/seed.hpp` | 哈希、数值种子、随机数 | STL |
| `libstl/time.hpp` | 本地时间、UTC ISO 时间 | STL、平台时间转换 |
| `libstl/encoding.hpp` | 兼容原有 Memade 编码 | STL |
| `libstl/packet.hpp` | 兼容原有 Packet 封包 | STL |
| `libstl/utils.hpp` | 字符串、地址、文件辅助方法 | STL、libpath |
| `libstl/main_proc.hpp` | 旧版阻塞控制台循环 | STL |

`cache.hpp`、`container.hpp` 是兼容转发入口。新增代码优先包含所需模块。
非模板实现放在 `include/libstl/detail/*.ipp`，静态库和 header-only 共用这一份实现，业务代码不应直接包含 `.ipp`。

默认保留静态库，适合当前有多个调用方的 Sovkit 工程：

```cmake
target_link_libraries(app PRIVATE sovrankit::libstl)
```

单个独立目标采用 header-only：

```cmake
target_link_libraries(app PRIVATE sovrankit::libstl_header_only)
```

整个工程切换（现有 `sovrankit::libstl` 接入名不变）：

```powershell
cmake -S . -B .build/workspace/vs2026 -DLIBSTL_HEADER_ONLY=ON
```

恢复默认使用 `-DLIBSTL_HEADER_ONLY=OFF`。不要在同一个可执行文件或共享库内混用两个目标；宏配置必须在所有翻译单元及其依赖中一致。

不用 CMake 时，将 `include` 加入头文件搜索路径，并为全部相关源文件定义 `LIBSTL_HEADER_ONLY=1`。需要分发整个 `include` 目录，header-only 不等于只有一个文件。使用完整入口或 `utils.hpp` 时仍需提供 `libpath` 的头文件与 Iconv 依赖；仅使用容器、缓存、种子时不需要它们。CMake 的纯模板依赖目标为 `sovrankit::libstl_core`。

## header-only 的取舍

可以实现，本库现已支持。模板部分原本就应放在头文件中。

| 方面 | header-only | 默认静态库 |
| --- | --- | --- |
| 集成 | 不需要构建、匹配 libstl 二进制库 | 需要先构建库 |
| 编译时间 | 每个使用方解析实现，改实现会触发更多重编译 | 非模板实现集中编译 |
| 优化 | 编译器可以直接看到实现；不保证一定更快 | 可使用链接时优化 |
| 体积 | 重复实例由链接器处理，仍需实测 | 非模板实现集中管理，也不保证体积一定更小 |
| 维护 | 实现随头文件发布，宏配置需保持一致 | 实现细节与公共声明分开 |
| DLL 边界 | 不应依赖内联单例跨 DLL 共享 | 静态库分别链接到多个 DLL 时同样不保证共享 |

当前项目建议保持默认静态库，按模块使用头文件；小型工具或独立消费者可选择 header-only。两种模式都不能用 `ProcessCache` 代替 SDK 明确的跨模块状态接口。

## 行为和兼容边界

- 队列容量 `0` 表示无限；满时淘汰最早元素。修复容量多保留一项、`pop_back()` 删除头部的问题。
- 修复 multimap 查询意外移走值、`get(n)` 多返回一项、尾部删除无效迭代器、从普通 multimap 构造失败；修复 list/set 自赋值及赋值时未锁住源容器的问题。
- 容器回调仍在锁内执行，以保持原有原子访问语义；不能在回调中重入同一个容器。`list::search()` 返回借用指针，需要调用方继续同步；新增 `search_copy()` 返回独立副本。容器持锁销毁元素的旧行为也仍需避免析构函数重入；对象缓存的替换、删除、清空则已明确在解锁后释放对象。
- 对象缓存按存入的准确类型取出，错误类型返回空，不再进行不安全强制转换；基类/派生类自动转换不受支持，应在存入时明确类型。Raw 指针不拥有对象。单例保留原来的模块生命周期存储，不依赖静态析构顺序。
- 整数解析修复前导空白越界；越界、尾部杂字符返回 `nullopt`。十六进制要求完整有效的偶数位，非法输入返回空串。端口超范围返回 `false`，失败不修改 host/port。
- `StringSplit` 按字面分隔符处理，保留首部/中间空字段，省略末尾空字段；大小写方法只转换 ASCII，保留 UTF-8 字节和非 ASCII 字符，不提供 Unicode case folding。
- `Seed` 修复 32 位移位越界及 bool 实例化失败，非有限输入映射到 `kMinSeed`，超出 uint32 表示范围的大数有界处理。保留原来可表示输入的映射，它不保证所有历史输入都落在 `[kMinSeed, kMaxSeed]`。随机区间反向、NaN 或不可表示时抛 `invalid_argument`；标准随机分布不保证不同 STL 实现产生同一序列。
- Packet 保留原有本机字节序及 `unsigned long` 布局，Windows 与 LP64 平台的格式仍不同。新增长度、位置、数量、重复键及总载荷校验，畸形数据失败时清空输出；有效旧格式保持兼容。跨平台协议应另外设计带版本的固定宽度格式，不能直接替换现有线格式。
- `Encrypt::Memade*` 实际是编码，不具备加密能力。有效编码输出兼容旧版，解码拒绝畸形字符/尾部位，保留结束符后附加文本的兼容行为。
- 文件读取按块处理，避免 `tellg()` 失败转为巨额长度、Windows `long` 截断和文本模式尾部补零。读取失败返回空串，旧接口仍不能区分空文件和错误。`MakeFile` 仍会截断已有文件，`WriteFile` 仍拒绝空内容。
- `GetReqID()` 保留原有时间映射，不承诺唯一性。旧 `MainProc` 构造时阻塞读输入，新的事件循环应使用 `libsys::ConsoleInput`。

## 验证

```powershell
cmake --build .build/workspace/vs2026 --config Debug --target libstl_default_test libstl_header_only_test --parallel 4
ctest --test-dir .build/workspace/vs2026 -C Debug -R '^sovkit.libstl\.' --output-on-failure
```

同一组回归分别链接默认目标和 header-only 目标，包含多翻译单元链接、各公共模块头文件独立编译、旧格式字节比对、截断包、容器边界、缓存类型/析构重入、二进制文件读写。测试仅位于 `tests/cpp`。

2026-09-24 本机验收：Windows x64、Visual Studio 2026 Debug 构建无警告；libstl 两种模式、libsys 三项测试、SDK 初始化及生命周期共 7 项通过，SDK 导出集合仍为 91 个符号。纯模板头文件另以 `/W4 /WX`、仅提供 libstl include 目录独立编译通过。SDK 工作目录测试需要常规用户权限环境，受限沙箱下会返回 `workspace_unavailable`。本次未在 Linux/macOS 上运行验证。
