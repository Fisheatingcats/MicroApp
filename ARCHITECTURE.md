# 核心与适配边界

```text
产品 App / 固件入口
         ↓
Host 适配：UI、日志、文件、设备、Flash、签名策略
         ↓
MicroApp 核心：注册、生命周期、导航、CRC、原生包结构与工具
```

## FreeWatch 中的代码归属

| 原有代码 | v0.0.1 归属 |
| --- | --- |
| `freewatch_dynamic_app.c` 的注册、切换、导航 | 抽到 `src/microapp.c`；原文件成为 service/LVGL 适配器 |
| `freewatch_dynamic_app.h` | 保留兼容入口和 FreeWatch 生命周期签名，info 类型引用 MicroApp |
| `fwapp_crc32.c` | 算法抽到 MicroApp；原文件保留转发函数 |
| `fwapp_abi.h` / `fwapp_format.h` | 权威定义抽到 MicroApp；原文件保留旧 include 路径 |
| `tools/fwapp/pack_fwapp.py` / `verify_fwapp.py` | 算法抽到 MicroApp；原脚本为 FreeWatch profile 兼容入口 |
| `fwapp_host_api.c` | FreeWatch 适配：绑定 LVGL、日志、二维码、马达、资源和返回导航 |
| `fwapp_native.c` | FreeWatch 适配：固定槽探测、APP_RAM 初始化、native main 调用 |
| `fwapp_installer.c` | FreeWatch 适配：FatFs / 外部存储 / 内部 Flash / Ymodem / 许可与签名策略 |
| `freewatch_carousel.c` | FreeWatch 产品 UI，不属于通用核心 |
| `freewatch_builtin_apps.c` | FreeWatch 产品应用：launcher、文件、马达、system |
| `product/freewatch/apps/*` | FreeWatch 示例 App 和板级链接布局 |
| `freewatch_memory_map.h` / iflash / Kconfig | FreeWatch 板级地址、驱动与配置 |

不把硬件适配文件移进 MicroApp 后假装它们已经通用。v0.0.1 的 native SDK 继承现有 LVGL Host ABI，通用核心可以完全不包含这个头文件。

## 后续使用远程项目

当前 FreeWatch 默认用 `05_Vendor/MicroApp`，上游为 `https://github.com/Fisheatingcats/MicroApp.git`。CMake 支持：

- `FREEWATCH_MICROAPP_SOURCE_DIR`：使用另一份本地 MicroApp checkout。
- `FREEWATCH_MICROAPP_REPOSITORY`：上游仓库 URL，默认是上述 MicroApp 仓库。
- `FREEWATCH_MICROAPP_USE_REMOTE=ON`：用 FetchContent 拉取指定仓库/ref；默认 OFF，使用本地 Vendor 源码。
- `FREEWATCH_MICROAPP_REF`：默认 `v0.0.1`，正式发布后可换成已验证的 commit SHA。

首版源码发布到上述仓库，并创建 `v0.0.1` 标签。FreeWatch 的 Vendor 目录使用 Git submodule 固定提交，来源及初始化命令见 UPSTREAM.md。FreeWatch 的打包构建会从所选 MicroApp checkout 使用工具，并传入自己的 `tools/fwapp/freewatch.json`。

独立仓库导出时整个 `05_Vendor/MicroApp` 目录可以作为仓库根；不需要复制 STM32 驱动、FreeWatch 服务、vendor 或 build。正式许可文本与权利人信息仍待确定；首版提交保留 LICENSING.md 方案说明，不授予未经授权的商业使用权。

## 与旧行为的差异

拆分时修正三项与生命周期直接相关的边界：失败的 create 不再提前写导航历史；Back 不再把当前 App 重新压入历史形成往返；重复启动活动 App 不再同时 create/destroy 同一描述符。回调期间立即递归执行和注册/注销被拒绝，延迟导航仍可使用。

这些变化已有纯 C 主机测试。FreeWatch 实际触摸和 native App 生命周期仍需上板验证；本次未烧录。当前主固件布局占用旧 native 槽的风险仍然存在，本次不恢复 native 安装或改变 Flash 分区。
