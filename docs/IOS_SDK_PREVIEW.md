# iOS libcrypt / libnet / libble 开发 SDK

2026-10-09 新增 iOS arm64 / 最低 iOS 15.0 的 Release Framework 安装与打包。
它们是 SDK 的动态库边界；OpenSSL、libsodium、libuv、ngtcp2 等提供者在相应
Framework 内静态链接，消费者不另行维护这些实现。Flutter 引擎仍由 libflui 运行时统一部署。

| SDK | 动态产物 | 消费者公开入口 |
| --- | --- | --- |
| libcrypt | `DevkitCrypt.framework` | `include/libcrypt/` 版本化 C ABI |
| libnet | `DevkitNet.framework` | `include/libnet/` 版本化 C ABI |
| libble | `DevkitBle.framework` | `include/libble/ble.h` 版本化 C ABI |

三个 Framework 与消费者 Framework 均放在 App 的 `Frameworks/` 中，由 App 所属团队签名。
不在动态 Framework 内嵌第二层 Framework；iOS rpath 为 `@executable_path/Frameworks`。
宿主需要部署完整 SDK 安装内容和许可，不只复制 Mach-O 文件。

## 生产侧打包

先使用锁定工具链/提供者将对应组件配置为 iOS arm64、最低 15.0、Release，并执行安装。
再从 devkit 根目录调用：

```sh
python3 tools/package_ios_sdk.py libcrypt --build /path/to/crypt-build \
  --dependencies /path/to/vcpkg-installed/arm64-ios13 --output /path/to/packages
python3 tools/package_ios_sdk.py libnet --build /path/to/net-build \
  --dependencies /path/to/vcpkg-installed/arm64-ios13 --output /path/to/packages
python3 tools/package_ios_sdk.py libble --build /path/to/ble-build --output /path/to/packages
```

打包器核验架构、Mach-O 平台及最低系统版本、公开导出集、动态依赖允许列表、
静态提供者摘要、许可和构建前后源码摘要，生成归档、清单及精确锁。提供者 triplet
为 iOS 13 不降低顶层 SDK 的最低 iOS 15 要求。

iOS 归档明确为 `development-preview`，清单记录来源提交、源码摘要及 `sourceDirty`。
从已提交且组件无修改的来源构建时，`sourceDirty` 为 false；开发中的构建如实标记为 true。
源码提交和清洁构建不等于已发布二进制 SDK。
打包检查证明可构建、可安装及二进制边界，不能替代每项 API 的真机验收。
Sovkit 在 iPhone SE / iOS 15.8.8 已加载这三项 Framework，libcrypt 的身份生成、
Vault 加解密经过真实启动调用；这不表示 libnet 的 TCP、QUIC 或全部密码 API 均已完成 iOS 验收。
libble 的 Mac 扫描 / iPhone 广播，以及 iPhone 扫描 / Mac 广播均已通过真实 GATT；
安全码双方确认、双向短文字、重启恢复及角色互换后的送达通过 Sovkit 实机验证。
身份及持久业务回执由 Sovkit 验证，SDK 的 ATT 完成不代表业务送达。长期后台尚未验收。
正式发布需使用已提交来源重打包、升级消费锁并执行对应回归，再完成发布渠道与签名验收。
消费端禁止修改依赖检出。
