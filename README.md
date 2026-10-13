# MicroApp

<p align="center"><img src="assets/logo.png" alt="MicroApp logo" width="320"></p>

MicroApp 是面向 MCU 的 App 管理框架与独立 ARM SDK。业务 App 可单独编译为 `.mapp` 安装包，通过宿主传入的函数表使用日志、GUI 或设备服务；主固件负责平台适配和安装管理。

**当前版本：`V0.0.2`** · 源码：[Fisheatingcats/MicroApp](https://github.com/Fisheatingcats/MicroApp)

支持安装多个 App，由一个生命周期管理器维护一个前台实例。原生宿主可在进入 App 时准备实例 RAM，退出后复用给下一个 App；同一份安装包可以在兼容宿主提供的不同 Flash/RAM 基址运行。

## 提供的能力

| 模块 | 职责 |
|---|---|
| ARM SDK | CMake 构建、PIC 链接、ELF 依赖与重定位审计、打包、校验和占用报告 |
| 原生装载接口 | 流式包校验、GOT/data/BSS 准备、保存和恢复 r9 的 ARM 调用桥 |
| 生命周期管理器 | 静态/动态注册、单前台生命周期、view 回调、延迟导航与历史管理 |
| 多 App 存储协议 | 追加提交、按 ID 更新/删除、枚举、空间整理与已提交备份恢复 |

核心、工具和无 GUI 示例不依赖 LVGL、HAL、RTOS 或产品工程。宿主提供 Flash 驱动、GUI/设备服务、调用上下文、权限和签名策略；App 只依赖双方约定的接口头文件。

当前原生构建基线为 **Arm GNU Toolchain 14.2、Cortex-M4 Thumb、freestanding C**。浮点 ABI 必须与宿主一致；SDK 不提供完整 libc、C++ 初始化、TLS 或外部动态库链接。

位置无关仍要求满足目标 ABI、容量及 Flash/RAM 对齐约束。每次创建实例都重新初始化 RAM 并修正 GOT/指针，Flash 正文保持不变；当前不支持活动实例热迁移，也不提供内存隔离。

## 快速开始：构建一个无 GUI App

先准备 Git、CMake 3.22+、Ninja、Python 3 和 Arm GNU Toolchain，将 `cmake`、`ninja`、`arm-none-eabi-gcc` 等工具加入 PATH。下面构建仓库自带的 counter 示例，App 仅通过注入的日志服务输出计数。

```sh
git clone https://github.com/Fisheatingcats/MicroApp.git
cd MicroApp
```

### POSIX shell

```sh
python3 -m venv build/venv
build/venv/bin/python -m pip install -r tools/requirements.txt
cmake -S examples/counter -B build/counter -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm-none-eabi.cmake" \
  -DPython3_EXECUTABLE="$PWD/build/venv/bin/python"
cmake --build build/counter
build/venv/bin/python tools/microapp/verify_microapp.py build/counter/counter.mapp \
  --profile examples/counter/target.json --json
```

### Windows PowerShell

```powershell
python -m venv build/venv
& ./build/venv/Scripts/python.exe -m pip install -r tools/requirements.txt
cmake -S examples/counter -B build/counter -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm-none-eabi.cmake" `
  -DPython3_EXECUTABLE="$PWD/build/venv/Scripts/python.exe"
cmake --build build/counter
& ./build/venv/Scripts/python.exe tools/microapp/verify_microapp.py build/counter/counter.mapp `
  --profile examples/counter/target.json --json
```

成功后得到 `counter.elf`、`counter.map`、`counter.mapp` 和 `counter.mapp.json`。报告列出 Flash 正文、GOT、data、BSS、总 RAM、修正记录数量和 ELF 审计结果。构建与校验不会把 App 安装到设备，也不会执行其 ARM 代码；实际执行需要宿主适配。

counter 配置采用 hard-float。切换浮点 ABI 时，在新构建目录中设置 `MICROAPP_ARM_FLOAT_ABI`，并同步修改 App 的 `target.json`，避免宿主工具链和 App profile 不一致。

## 构建自己的 App

App 工程使用 ARM 工具链配置，并通过 `microapp_add_arm_app()` 声明源码、接口头、清单与资源：

```cmake
add_subdirectory(path/to/MicroApp microapp)
microapp_add_arm_app(my_app
    SOURCES main.c helper.c
    HOST_HEADERS host_api.h
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    PROFILE ${CMAKE_CURRENT_SOURCE_DIR}/target.json
    MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/app.json
    RESOURCES message=${CMAKE_CURRENT_SOURCE_DIR}/message.txt)
```

- `host_api.h`：宿主与 App 共享的服务表类型和调用约定，服务表以 `microapp_abi_header_t` 的三个字段开头。
- `target.json`：架构、浮点 ABI、宿主 ABI、容量和权限名称；`board_id` 表示宿主接口配置标识，不保存最终安装地址。
- `app.json`：App ID、名称、版本、作者、描述和请求权限。

完整文件见 [counter 示例](examples/counter)。原生入口固定为：

```c
int32_t microapp_main(const void *host_api, microapp_abi_context_t *context,
                     uint32_t reason, void *arg);
```

App 检查服务表的 ABI 与尺寸，将 `host_api` 转为约定的类型，通过函数指针调用宿主。不要直接链接宿主实现的绝对地址；严格链接和 ELF 审计会拒绝未解析依赖、需要修改 Flash 的重定位及不支持的结构。

## 宿主如何接入

App 仅需 `MicroApp::sdk` 的头文件；宿主链接 `MicroApp::core`，提供 view、驱动、服务表和实例管理。公共接口分布在 [生命周期](include/microapp/microapp.h)、[原生装载](include/microapp/microapp_native.h) 和 [存储](include/microapp/microapp_store.h) 头文件中。

1. **验证与安装**：先用 `microapp_validate_package()` 检查完整包，再执行宿主的权限/签名策略。使用通用存储时，将清单和正文 reader 交给 `microapp_store_install()`；它追加记录、写入原样正文、读回校验，最后提交记录。失败包不得先触发擦除。
2. **创建实例**：选择满足容量和对齐的 RAM，调用 `microapp_prepare_ram()` 准备 GOT、初始化数据和 BSS。已安装但未进入的 App 保留 Flash 和注册元数据，不需要该 App 的实例 RAM。
3. **调用与销毁**：生命周期及延迟回调通过 `microapp_arm_call()` 进入 App。宿主保活服务表和上下文，并在销毁时停止事件源、拒绝旧回调、释放对象和回调存储。
4. **导航与互斥**：GUI 事件使用 `microapp_request_*()`，事件派发结束后由 `microapp_tick()` 提交切换。核心的同步重入标志不是线程锁，也不自动保护所有 GUI 回调；调用与存储操作由宿主串行化。

切换顺序为旧实例 PAUSE/DESTROY 后新实例 CREATE，允许共享 App RAM。空 view 分配失败时保留旧 App；新 CREATE 失败时尝试重建第一个注册的 App（宿主应先注册 Home），失败 CREATE 的局部资源由 App/适配器清理。

多 App 记录按相对偏移遍历，采用最新已提交版本和删除标记。整理需要独立备份区，容量至少为主区大小加 4096 B；没有备份仍可追加和删除，但不能整理。存储回调必须同步持久化，擦写/恢复期间不得从相关区域执行 App，搬移后需重新枚举代码和资源位置。

管理器提供 8 个静态槽和宿主持有的动态链表节点，导航历史最多 8 项；动态注册数量受实际 Flash/宿主内存限制。核心不分配堆、不创建任务，也不自带 GUI。CRC 用于完整性校验，签名认证由宿主实现。

## 测试

以下命令面向 x86/x86_64 主机，使用本机 GCC；Windows 可使用已加入 PATH 的 MinGW。它们不执行 ARM 包的机器码。

POSIX shell：

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_C_COMPILER=gcc \
  -DMICROAPP_BUILD_TESTS=ON -DPython3_EXECUTABLE="$PWD/build/venv/bin/python"
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Windows PowerShell：

```powershell
cmake -S . -B build/host -G Ninja -DCMAKE_C_COMPILER=gcc `
  -DMICROAPP_BUILD_TESTS=ON -DPython3_EXECUTABLE="$PWD/build/venv/Scripts/python.exe"
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

覆盖注册/导航/单实例、RAM 修正、包校验、多 App 更新/删除/整理恢复及存储故障注入。Python 工具测试需要上述虚拟环境；ARM 编译器可用时还会在独立临时目录构建真实示例并审计 ELF，缺少 ARM 编译器时该组跳过。主机测试和软件故障注入不能代替目标板执行、驱动及真实断电验收。

## 文档与版本

- [软件架构教程](docs/architecture-tutorial.md)：分层、对象生命周期、接口注入、GOT/r9、安装事务、恢复和宿主移植。
- [核心与宿主边界](ARCHITECTURE.md)：快速查看框架和产品各自的职责。
- [Doxygen 文档生成](docs/doxygen.md)：生成带类型、参数契约和源码浏览的 HTML。
- [更新记录](CHANGELOG.md)：版本变化与功能历史。

库版本、App 版本、宿主 ABI 与包格式分别管理。本版库和提供的示例 App 使用 `V0.0.2`；CMake 数字版本及 major/minor/patch 为 `0.0.2`，包格式为 2.0。App 的后续版本可独立演进。

## 许可与商业授权

正式许可证和商业授权条款尚未定稿。授权方向与待确定事项见 [LICENSING.md](LICENSING.md)，该文件是方案记录，当前不构成正式使用许可。
