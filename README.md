# MicroApp

<p align="center"><img src="assets/logo.png" alt="MicroApp logo" width="320"></p>

版本 `0.0.2`。MicroApp 提供 MCU App 生命周期管理，以及独立的 ARM App 构建、链接、打包、检查和校验 SDK。公共代码、工具和无 GUI 示例不依赖 LVGL、HAL、RTOS 或产品工程。

源码：[Fisheatingcats/MicroApp](https://github.com/Fisheatingcats/MicroApp)。首版 `v0.0.1` 标签保持不变。正式许可与商业授权条款待定，见 [LICENSING.md](LICENSING.md)。

## 教程与 API 文档

- [软件架构完整教程](docs/architecture-tutorial.md)：分层、三种 App 对象、生命周期、接口注入、ELF/PIC、GOT/r9、安装事务、恢复与宿主移植。
- [Doxygen 文档生成与注释维护](docs/doxygen.md)：从库根目录执行 `doxygen Doxyfile`，生成带模块、类型、参数与源码浏览的 HTML。

公共头文件记录接口契约，核心实现与 counter 示例使用 Doxygen 风格注释。注释与教程以当前 0.0.2 实现为准。

## 独立构建 ARM App

基线：Arm GNU Toolchain 14.2、Cortex-M4 Thumb、Python 3、CMake 3.22+、Ninja。只支持 freestanding C；没有完整 libc、C++ 初始化、TLS 或外部动态库。浮点 ABI 由 target.json 明确声明，并由打包工具核对 ELF 属性。

```sh
python -m pip install -r tools/requirements.txt
cmake -S examples/counter -B build/counter -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm-none-eabi.cmake"
cmake --build build/counter
python tools/microapp/verify_microapp.py build/counter/counter.mapp \
  --profile examples/counter/target.json --json
```

Windows 可传入绝对路径的 toolchain 文件；使用虚拟环境时，同时设置 `-DPython3_EXECUTABLE=<python 的绝对路径>`。独立工具链默认宿主 hard-float，构建另一种宿主 ABI 时在首次配置设置 `MICROAPP_ARM_FLOAT_ABI`；App ABI 仍以 profile 为准。输出 `counter.elf`、`counter.map`、`counter.mapp` 和 `counter.mapp.json`。报告包括代码/正文 Flash、GOT、data、BSS、总 RAM、修正项数量和 ELF 审计结果。

项目集成：

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

`target.json` 声明宿主接口配置：`board_id`、`architecture: "armv7e-m"`、`float_abi: "soft"/"softfp"/"hard"`、`abi_major`、`abi_minor`、`host_version`、`max_body_size`、`max_ram_size` 和单比特权限名称。它没有物理安装地址。`app.json` 声明 `app_id`、`name`、`version`、`author`、`description` 和权限名称列表 `permissions`；完整示例见 [examples/counter](examples/counter)。

入口固定为：

```c
int32_t microapp_main(const void *host_api, microapp_abi_context_t *context,
                     uint32_t reason, void *arg);
```

App 将 host_api 转为自己的宿主接口表。Host 表以 `abi_major / abi_minor / struct_size` 开头，服务通过函数指针传入；权限位及服务语义由宿主管理。`context->host_user` 由宿主持有，生命周期覆盖延迟回调。SDK 示例只接收日志函数，包含初始化变量、BSS、字符串指针、函数指针、.data.rel.ro 和跨源文件调用。

## GOT + r9 与格式 2.0

编译使用 `-fPIC -msingle-pic-base -mpic-register=r9 -mno-pic-data-is-text-relative`。链接使用 ET_DYN、`-Bsymbolic --no-undefined --emit-relocs`，内部调用静态绑定，保留真实重定位记录。

打包工具通过 pyelftools 审计 ELF 的段、符号、ARM 属性和重定位。仅支持模板产生的内部 PC 相对调用/引用、GOT 引用和 RAM `R_ARM_RELATIVE`；未解析符号、外部绝对引用、Flash 修正、未知重定位、PLT、TLS 和构造器被拒绝。链接器的 `0x10000000 / 0x20000000` 是审计用逻辑区间，不会保存为最终地址。

2.0 `.mapp` 由 56 B 包头、312 B 清单和不可变正文组成。正文从代码和只读数据开始，随后是初始化 RAM、12 字节 RAM 修正记录和资源表。清单偏移相对于正文或实例 RAM；入口和 Flash 函数指针保留 Thumb 最低位。代码与 RAM 的对齐要求从 ELF 节属性推导并由宿主核对。CRC 覆盖清单、正文、资源和修正记录。没有扫描镜像猜测指针的逻辑。旧 1.0 包明确拒绝。

GOT 位于实例 RAM。创建实例时，SDK 清零 RAM、复制初始化数据，再根据修正记录写入实际 Flash/RAM 指针。r9 指向链接器定义的 `_GLOBAL_OFFSET_TABLE_`。.data.rel.ro 的指针也在 RAM 中准备。相同包在不同位置运行时，Flash 正文逐字节保持不变。

## 宿主装载与调用

链接 `MicroApp::core` 可使用 `microapp_native.h` 中的流式校验与 RAM 准备接口；ARM 编译时包含汇编调用桥。只需要头文件的 App 链接 `MicroApp::sdk`。

1. 为包提供有长度限制的随机读取器，调用 `microapp_validate_package()` 核对目标 ABI、容量、CRC、资源和 RAM 记录；宿主检查签名策略。
2. 宿主选择槽内正文位置，写入原样正文、读回校验，最后提交槽头。失败包不得先擦除安装槽。
3. 宿主选择容量足够且满足对齐的 RAM 区间，调用 `microapp_prepare_ram()`。每次创建都重新准备 RAM。
4. 生命周期通过 `microapp_arm_call(got, entry, host_api, context, reason, arg)` 进入 App；桥保存、设置和恢复 r9，并保持 AAPCS 栈对齐。
5. 延迟事件也必须走调用桥。宿主保存原始 user_data，并在实例销毁后拒绝其回调；对象删除和回调存储释放由宿主负责。

Flash 驱动、安装入口、GUI/设备服务、签名验证和事件调度属于宿主适配。当前只支持一个活动原生实例；其他已安装 App 只保留 Flash 正文和注册元数据，不占实例 RAM。CRC 是完整性检查；签名与授权由宿主控制。

## 多 App 存储

可选的 `microapp_store.h` 提供独立的追加写入、按 ID 更新/删除、枚举和空间整理协议。宿主传入有边界的同步 read/write/erase 回调、Flash 区域和目标配置；写入只能将位从 1 清为 0，驱动返回前必须完成持久化。所有操作由宿主串行调用，整理和恢复时不得有 App 在该区域执行。

每条记录有 512 B 容器头和按要求对齐的原样正文。记录通过区域相对偏移和长度遍历，最新已提交记录决定该 ID 的版本；提交字段最后写入，删除使用标记。未提交更新不会替换旧 App，删除后旧版本也不会重新出现。坏包先拒绝，空间不足则保留既有内容。更新需要同时容纳旧版本和新版本。

整理需要独立备份区域，容量至少为主区域大小加 4096 B。协议先复制有效记录、校验并提交备份，再擦写主区域，最后标记备份已消费；启动时 `microapp_store_open()` 恢复已提交但未完成的整理。正文不随搬移改变，实例启动时按新地址准备 GOT/RAM。没有备份时仍可追加和删除，不能整理。宿主决定备份介质、容量、安装权限和运行互斥；驱动必须满足上述持久化契约。

容器版本与 `.mapp` 格式 2.0 分开管理；有效的旧格式 2.0 单槽可在整理时迁移，1.0 包仍然拒绝。

## 生命周期管理器

`microapp.h` 是独立的进程内管理器：单前台 App，保留 8 个静态系统 App 槽，动态 App 使用宿主持有的链表节点，没有固定注册数量上限。核心无内部堆分配、任务或锁；数量由宿主存储和内存决定，导航历史单独限制为 8 项。宿主提供 create_view/delete_view/show_view，单任务调用；描述符、字符串、节点和用户数据在注册期间保持有效。

动态注册使用零初始化的 `microapp_registration_t` 和 `microapp_register_dynamic()`；注销成功后节点清空，活动 App 的注销返回 BUSY。静态注册仍使用 `microapp_register()`。

切换先分配空 view，再执行旧 App pause/destroy → 新 App create → 显示新 view → 删除旧 view → 新 App resume。这样新 App CREATE 前旧实例已释放，允许多个已安装 App 复用同一片 RAM。空 view 分配失败保留旧 App；CREATE 失败时清理失败 view，重新创建第一个注册的系统 App（宿主应先注册 Home）。App 自己负责失败 CREATE 的局部资源清理。事件中使用 `microapp_request_*` 延迟导航，再由 `microapp_tick()` 提交；立即递归切换返回 BUSY。

## 测试

```sh
cmake -S . -B build/host -G Ninja -DMICROAPP_BUILD_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

包括生命周期核心、32 个动态注册节点、单实例 CREATE 顺序、2.0 流式校验、四组地址 RAM 准备、多 App 更新/删除/整理恢复、已提交非法记录拒绝和写入故障注入。安装 ARM 工具链后，Python 测试会将 SDK 导出到临时独立目录构建真实示例，审计指针修正和依赖，并检查非法 ELF/严格未解析符号拒绝；没有 ARM 工具链时明确跳过该组。
