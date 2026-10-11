# Changelog

## v0.0.1 — 2026-10-11（首版源码提交）

- 从 FreeWatch 抽出不依赖 RTOS/GUI/板级服务的 C11 生命周期与导航核心。
- 提供 MicroApp::core 和 MicroApp::sdk CMake 目标及独立主机测试。
- 抽出 CRC32、兼容 native Host ABI 和格式 1.0 结构、显式 profile 的打包/验证工具。
- FreeWatch 保留旧接口，通过适配器使用 MicroApp。
- 修正失败创建的历史记录、Back 往返、同 App 重复创建及立即重入边界。
- 固定地址 native 执行行为、包格式 1.0 和旧 Host ABI 1.2 保持不变；无 PIC 或重定位。
- 记录非商业使用与单独商业授权方向，正式许可证未发布。
