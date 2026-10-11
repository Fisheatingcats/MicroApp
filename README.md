# MicroApp

版本：`v0.0.1`。一个面向 MCU 的小型 C 应用生命周期与导航框架，从 FreeWatch 的 App 框架拆分而来。

首版源码发布于 [Fisheatingcats/MicroApp](https://github.com/Fisheatingcats/MicroApp)，版本标签为 `v0.0.1`。FreeWatch 通过 `05_Vendor/MicroApp` Git 子模块固定依赖提交，见 [UPSTREAM.md](UPSTREAM.md)。许可方向是非商业使用许可加单独商业授权，正式文本待定，见 [LICENSING.md](LICENSING.md)。

## v0.0.1 提供什么

- 单前台 App、最多 8 个注册描述符；无内部动态内存分配。
- create / resume / pause / destroy / tick 生命周期。
- 延迟 launch / home / back 请求，以及有限长度的导航历史。
- 通过 view port 接口接入 UI，不依赖 LVGL、HAL、RTOS 或 FreeWatch 服务。
- 增量 IEEE CRC32。
- 原有 `.fwapp` 格式 1.0 的元数据结构、资源结构与打包/验证工具。
- 可选的旧 `.fwapp` Host ABI 1.2 头文件；该接口使用外部 LVGL 类型，不属于通用核心接口。

v0.0.1 不包含 ELF 装载器、PIC/r9/GOT、安装时重定位、多 App 并发或硬件 Flash 驱动。现有 FreeWatch 原生执行路径仍是固定地址。

## 目录

```text
MicroApp/
├── CMakeLists.txt
├── VERSION
├── include/microapp/
│   ├── microapp.h         # 通用注册、生命周期、导航、port
│   ├── microapp_crc32.h   # CRC32
│   ├── fwapp_format.h    # 格式 1.0 的磁盘结构，独立于板级地址
│   └── fwapp_abi.h       # 兼容原有 LVGL Host ABI，需外部 lvgl.h
├── src/
├── tools/fwapp/           # 使用显式板级 JSON profile 的工具
├── tests/
├── ARCHITECTURE.md
├── CHANGELOG.md
└── LICENSING.md
```

## 独立构建和测试

需要 C11 编译器、CMake 3.22+；Ninja 是下列命令的构建器。核心不需要 ARM 工具链、LVGL 或父工程。

```sh
cmake -S . -B build -G Ninja -DMICROAPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 集成

```cmake
add_subdirectory(path/to/MicroApp)
target_link_libraries(your_application PRIVATE MicroApp::core)
```

只使用原生包头文件时链接 `MicroApp::sdk` 接口目标，无须把生命周期核心链接进 `.fwapp` 镜像。

Host 实现 `microapp_port_t` 的 create_view / delete_view / show_view，提供自己的 `host` 指针，然后注册 App 描述符：

```c
microapp_t manager = {0};
microapp_status_t rc = microapp_init(&manager, &port, host);
/* Check rc, register long-lived descriptors, and launch the first (home) App. */
```

用 `microapp_request_*()` 从事件回调中请求导航；在 UI 事件处理结束后调用 `microapp_tick()` 执行请求。任务退出时用 `microapp_deinit()` 释放活动 view 并调用 pause/destroy。

## 生命周期约定

- `microapp_t` 首次使用必须清零。一个实例只由一个任务/线程操作，不提供内部锁。
- port、App 描述符、info 字符串和 user 数据由调用方持有，必须在注册期间有效且保持稳定；框架不复制它们。
- 第一个注册 App 是 home。所有注册 App 必须有非空 ID 和 create 回调。
- create 失败时负责撤销其自身已申请的资源；框架删除失败的 view，不调用失败实例的 destroy，旧 App 保持活动。
- 切换顺序为创建新 view/App → 旧 App pause/destroy → 显示新 view → 删除旧 view → 新 App resume。
- 重复启动当前 App 返回成功但不重新创建；同一个描述符不支持同时存在两个实例。
- 回调内允许延迟导航，禁止立即递归 launch、tick、注册、注销或 deinit；返回 BUSY。
- 延迟请求只保留最后一个。queued ID 会解析成已注册描述符，不保留调用方临时字符串。
- 导航历史最多 8 项；满时不继续记录，Back 最终回 home。Back 与 Home 不将离开的页面再次压栈。
- show_view 是不可失败的提交步骤；port 必须提供可用的实现。框架不创建任务，也不拥有 App 代码镜像。

## 原生包工具

`tools/fwapp/pack_fwapp.py` 和 `verify_fwapp.py` 都要求 `--profile board.json`；pack 还需要 ARM ELF、binary 和 `arm-none-eabi-nm`。

```json
{
  "board_id": "your_board",
  "slot_base": "0x08060000",
  "slot_size": "0x20000",
  "image_offset": "0x200",
  "ram_base": "0x2001E000",
  "ram_size": "0x2000"
}
```

这些地址仅是格式示例；必须由具体板级适配决定，不能直接复制到其他硬件。它们用于核对镜像的链接地址，不提供镜像重定位能力。

```sh
python tools/fwapp/pack_fwapp.py --help
python tools/fwapp/verify_fwapp.py app.fwapp --profile board.json
```

框架版本 `0.0.1`、旧 Host ABI `1.2` 和包格式 `1.0` 是三个独立版本。本次拆分保留 ABI 和包结构字节布局，不重命名 FWAPP 磁盘标识或符号。
