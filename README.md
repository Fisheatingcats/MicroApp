# MicroApp

<p align="center">
  <img src="assets/logo.png" alt="MicroApp logo" width="320">
</p>

版本：`0.0.1`。面向 MCU 的小型 C 应用生命周期与导航框架。

源码：[Fisheatingcats/MicroApp](https://github.com/Fisheatingcats/MicroApp)。当前 main 已整理公共命名和宿主边界；首版 `v0.0.1` 标签保留原始源码快照，当前接口应固定后续提交。正式许可待定，见 [LICENSING.md](LICENSING.md)。

## 功能和目录

- 单前台 App、最多 8 个描述符；无内部堆分配或任务。
- create / resume / pause / destroy / tick 生命周期、延迟导航和返回历史。
- `include/microapp/microapp.h`：进程内管理器和 view port。
- `include/microapp/microapp_abi_port.h`：native 结果码、生命周期原因、上下文和 Host 表头；宿主定义服务表和入口签名。
- `include/microapp/microapp_format.h`：格式 1.0 的包、资源和安装槽元数据。
- `include/microapp/microapp_crc32.h`：增量 IEEE CRC32。
- `tools/microapp/`：pack_microapp.py、verify_microapp.py、microapp_profile.py。

公共头文件不依赖 GUI、HAL、RTOS 或产品服务。框架未实现 native 装载、Flash 操作、签名策略、PIC/r9/GOT 或安装时重定位；这些由宿主负责。

## 构建与集成

需要 C11 编译器、CMake 3.22+；工具测试需要 Python。

```sh
cmake -S . -B build -G Ninja -DMICROAPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

```cmake
add_subdirectory(path/to/MicroApp)
target_link_libraries(your_application PRIVATE MicroApp::core)
```

只使用 ABI/包头文件时链接 MicroApp::sdk，不需要把管理器链接进 native 镜像。

## 管理器约定

宿主实现 microapp_port_t 的 create_view / delete_view / show_view 并提供 host 指针。microapp_t 首次使用清零，只由一个调用任务操作，无内部锁。port、App 描述符、info 字符串和 user 数据在注册期间有效且保持稳定。

第一个 App 为 home。切换顺序：创建新 App → 旧 App pause/destroy → 显示新 view → 删除旧 view → 新 App resume。create 失败负责清理自身非 view 资源；框架删除失败 view，保留旧 App。show_view 是不可失败的提交步骤。重复启动活动 App 不重新创建。

回调中用 microapp_request_* 延迟导航，事件处理后调用 microapp_tick，退出时调用 microapp_deinit。立即递归切换、注册/注销或 deinit 返回 BUSY。只保留最后一个导航请求；存储已注册描述符，不保留临时 ID 字符串。历史最多 8 项；Back/Home 不再次压入离开的 App。

## Native ABI port

microapp_abi_port.h 的结果码用于 native 调用边界，与管理器 microapp_status_t 分开。Host 表以 abi_major / abi_minor / struct_size 开头；具体服务类型、权限位和入口签名由宿主定义。host_user 由宿主持有，上下文生命周期须覆盖延迟回调。ABI 版本由宿主管理，与框架和包格式版本独立。

GUI、日志、设备函数表位于宿主 SDK；MicroApp 不规定具体 GUI 库、字体、电机或二维码接口。

## 包工具

两个工具要求 --profile host.json；pack 还需要 ARM ELF、binary 和 arm-none-eabi-nm。Profile 包含板级几何、宿主 ABI、host_version、命名权限位和可选 ELF 符号映射：

```json
{
  "board_id": "your_board",
  "slot_base": "0x08060000",
  "slot_size": "0x20000",
  "image_offset": "0x200",
  "ram_base": "0x2001E000",
  "ram_size": "0x2000",
  "abi_major": 1,
  "abi_minor": 0,
  "host_version": 1,
  "permissions": {"DISPLAY": 1}
}
```

地址仅为格式示例，须由硬件适配确定；工具核对固定链接地址，不提供重定位。host_version 用于写入最低宿主版本及校验当前宿主能力；打包和校验时可提供不同版本的 profile。

默认符号：microapp_main、__microapp_text_start、__microapp_text_size、__microapp_data_load、__microapp_data_size、__microapp_data_start、__microapp_bss_size。用 symbols 字典的 entry / text_start / text_size / data_load / data_size / data_start / bss_size 覆盖名称。权限值必须为互不重复的 32 位单比特数；未知权限、ABI 不兼容和范围错误被拒绝。

```sh
python tools/microapp/pack_microapp.py --help
python tools/microapp/verify_microapp.py app.mapp --profile host.json
```

公共源码使用 microapp 命名，格式 1.0 保持原字段布局和磁盘 magic 值，已有包可由正确宿主 profile 校验。旧源码兼容头由宿主提供。
