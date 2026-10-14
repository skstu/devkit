# devkit

This repository is the authoritative development location for shared native libraries.
Consumers pin a commit and must not edit their dependency checkout in place.
Keep component public headers, existing target names and license notices compatible.
All components use the root VERSION; 0.x minor releases may break source compatibility,
patch releases must preserve it. No cross-toolchain C++ binary ABI is promised.
Keep business identity, pairing, trading state and product policy in consumers.
Build out of tree. Do not access real user identities, credentials or databases in tests.
libengjs and the full libcompr archive backend are retained legacy code, not validated releases.

libflui exposes a versioned C ABI; do not expose Flutter, Objective-C, or C++ types.
Keep its renderer, engine lock and runtime packaging together. Consumers must not
maintain Dart projects for it. Build output stays outside source. macOS is the
initial backend; other platforms require implementation and validation.

## 构建存储约定（2026-10-14，用户明确更新）

- 每个平台只保留 Debug、Release 各一个固定槽位，下次构建直接覆盖；不保留回滚包、previous-*、版本号或日期命名的安装包副本。
- 同一工具链与配置复用固定工作目录，不复制整套源码、Flutter 宿主或 SDK。当前三端使用 SovKit 的 `scripts/build_zhiyu.py`，禁止再复制历史版本构建助手。
- SDK 与其锁放在固定依赖槽位，原始 Flutter 引擎缓存共享。被当前 SDK 引用的旧路径可保留为链接，不能在清理时删除链接指向的唯一依赖。
- 完成签名导出后清掉 Runner.app、临时 Payload 和校验副本；Mac 的应用目录和桌面入口使用链接，不再备份或复制完整 App。
- 只保留必要的小型日志、版本、指纹和失败记录；源码由 Git 保存。不清理源码、未提交修改、证书、身份、信任关系、消息、接收文件或其他项目数据。
- 本机 Flutter SDK、Pub/Dart/Gradle 缓存、Android SDK／模拟器、CocoaPods 与构建临时目录集中在 `/Users/martell/Developer/`；旧默认路径只可作兼容链接。不得再在 Documents/Codex 中生成 Flutter 宿主、符号或安装包。
- 本约定优先于历史交接中的保留旧包／复制目录步骤。清理结果与固定路径见 README；每次完成构建只更新固定日志、指纹和校验记录，不新建一整份工作区。
- 通用 libflui 宿主默认目录为 `.build/work/libflui/<consumer>/<platform>/<debug|release>/`，消费者标识按规范化源码路径生成，不随版本或提交变化。
