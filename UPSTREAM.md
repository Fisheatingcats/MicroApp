# MicroApp 上游来源

- 仓库：https://github.com/Fisheatingcats/MicroApp.git
- 工程版本：`0.0.1`
- 源码标签：`v0.0.1`
- FreeWatch 依赖目录：`05_Vendor/MicroApp`

首版源码于 2026-10-11 提交到上游。FreeWatch 使用 Git submodule 固定具体提交；构建默认使用本地子模块 checkout，也可通过 CMake FetchContent 选择固定版本。正式许可证仍待定稿，见 LICENSING.md。

在 FreeWatch 仓库根目录初始化依赖：

```sh
git submodule update --init --recursive -- 05_Vendor/MicroApp
```

整个 MicroApp checkout 可独立构建，不需要 FreeWatch 的驱动、服务或其他 Vendor 库。框架 0.0.1 不改变旧 native Host ABI 1.2 和包格式 1.0，也不提供 PIC/重定位。
