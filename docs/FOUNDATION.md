# 自研基础库

这些目录是项目维护的基础库。SDK 负责身份、配对、授权、传输状态和存储模型；基础库提供不依赖 SDK 私有头的通用能力。自有 C++ JSON 解析和序列化统一使用 nlohmann JSON。

| 能力 | 头文件 | 源码构建目标 |
| --- | --- | --- |
| Hex、UTF-8、字节序、时钟 | `libstl/hex.hpp`、`text.hpp`、`binary.hpp`、`clock.hpp` | `sovrankit::libstl_core` |
| 有界执行器、字节额度 | `libstl/executor.hpp`、`byte_budget.hpp` | `sovrankit::libstl_core` |
| 标准文件系统 UTF-8 路径 | `libpath/utf8.hpp` | `sovrankit::libpath_utf8` |
| 随机、签名、密钥清理 | `libcrypt.h` | `sovrankit::libcrypt` |
| 私有文件/目录、原生句柄、原子无覆盖发布 | `libsys/private_directory.h`、`native_file.h`、`file_publication.h` | 私有目录锁使用 `sovrankit::libsys_file_lock` |
| 文件状态、同步、系统资源观测 | `libsys/file_io.h`、`resources.h` | `sovrankit::libsys_file_io` |
| 独占事件循环 | `libuvbrg/event_loop.h` | `libuvbrg::loop` |
| HTTP 策略 | `libnet_http_client.h` | `sovrankit::libnet_http` |
| DNS 名称和 TXT 编码 | `libnet_dns.h` | 头文件；使用 libnet 的 include 目录 |
| 流式复制、目录遍历 | `libcompr/stream.hpp` | `sovrankit::libcompr_stream` |
| SQL 语句/事务资源管理 | `libdb/libdb.h` | `sovrankit::libdb` |

以上为 `add_subdirectory` 后的目标，不表示每个组件都有独立的 `find_package` 配置。SDK 使用的纯 UTF-8 路径、STL 核心和流式复制组件不会引入 Iconv 或整套压缩库。旧 `libpath/path.hpp` 的显式编码转换仍按需依赖 Iconv。

基本用法：

```cpp
std::array<std::uint8_t, 2> value{};
if (!stl::Unhex("aaff", value)) return false; // 默认仅小写
const auto text = stl::Hex(value);
stl::binary::Reader reader(packet);
std::uint32_t length{};
if (!reader.U32(length)) return false; // 失败不消费游标或改写 length

stl::BoundedExecutor io(2, 32, stl::BoundedExecutor::Shutdown::Drain);
auto future = io.Submit([] { return 42; });
const auto result = future.get();
```

接口约定：

- Hex 大小写策略由调用方选择；`Either` 用于原本就兼容大写的接口，协议校验不自动放宽。
- `Unix*` 用于时间戳，`Steady*` 用于进程内超时。不能跨时钟比较。
- 执行器必须在生产者停止后由拥有者销毁；析构等待运行中任务。`Drain` 排空队列，`CancelPending` 使待执行任务的 future 进入 broken-promise 状态。任务不能在自己的执行器线程中销毁或等待该执行器。
- 字节额度的共享租约在最后一个引用释放时归还额度；租约本体不可复制。`Acquire` 使用 FIFO 标签，`Reserve` 不参与该等待队列。
- 文件发布只承诺当前平台支持的原子禁止覆盖操作；文件内容同步、父目录同步及业务恢复记录由调用方编排。Windows 发布使用 write-through，目录同步接口在该平台不另开目录 fsync。
- 私有目录组件检查当前所有者与链接限制，调用方保留祖先句柄、决定允许的根路径和目录布局。
- HTTP 的代理、重定向、CA、响应体上限和取消均可显式配置；地址是否获准使用由业务层判断。
- EventLoop 的 Observer 只报告通用事件；SDK 将其映射到业务日志。循环实例需由拥有者在循环线程之外销毁。桥接库已有的共享循环池保持独立生命周期。
- 流式归档基础组件不定义业务包格式，不依赖密码库；摘要/进度通过观察回调接入，业务负责命名许可、签名及最终发布。
- libdb 不再提供 SovKit 业务模型。项目内旧 Store 消费方改为 SDK 的 `storage_repository.h` 和 `Sovkit::db`；SDK 对外 C ABI 保持不变。

契约验证见 `tests/cpp/foundation_reuse_test.cpp`、`http_policy_test.cpp` 及现有 SDK、存储、工作目录和传输测试。

整改后的跨平台复核、修复及实际执行边界见 [兼容性审查回执](https://github.com/memade/sovkit/blob/main/docs/review/SDK_3RDPARTY_COMPAT_REVIEW_20260925.md)。libsys 独立宿主可通过 `LIBSYS_ENABLE_FILE_IO=ON` 启用文件 I/O/资源观测组件。
