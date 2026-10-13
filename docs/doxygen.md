# Doxygen 文档生成与维护 {#microapp_doxygen_guide}

MicroApp 的公共 C API 与核心实现使用 Doxygen 风格注释。入口为仓库根目录的 `Doxyfile`，架构教程和源码引用一起生成 HTML，输出到忽略的 `build/doxygen/`。

## 生成

在 MicroApp 根目录执行。先创建输出目录，使警告日志在首次生成时也能打开：

```sh
mkdir -p build/doxygen
doxygen Doxyfile
```

PowerShell 对应命令：

```powershell
New-Item -ItemType Directory -Force build/doxygen | Out-Null
doxygen Doxyfile
```

打开 `build/doxygen/html/index.html`。XML 同步输出到 `build/doxygen/xml`，便于检查文档覆盖。当前配置把警告写到 `build/doxygen/warnings.log`。配置把未记录 API、错误参数和文档警告视为失败，命令应返回零且警告日志为空。

本轮生成验证使用 Doxygen 1.18.0 Windows x64 便携版，来源为 [Doxygen 官方下载页](https://www.doxygen.nl/download.html)，校验了其官方 SHA256。Doxygen 是文档工具依赖，不是 App/宿主运行依赖。配置不要求 Graphviz；教程图以独立 SVG 保存在 `docs/diagrams`，也能在 Markdown 中查看。

## 组织与覆盖

| 模块组 | 内容 |
|---|---|
| Lifecycle and registry | 静态/动态注册、view、单实例、导航与借用生命周期 |
| Native loader primitives | reader、兼容策略、包校验、RAM 修正与 r9 桥 |
| Multi-App persistent storage | 回调契约、提交/删除、枚举与备份恢复 |
| Wire format 2.0 | packed 字段的长度、偏移原点、CRC 与 Thumb 语义 |
| Native host ABI | 服务表前缀、上下文与标准入口 |
| CRC32 | 流式 IEEE CRC32，明确其不负责认证 |
| Counter example | 无 GUI 宿主接口、初始化/BSS/指针与跨文件调用 |

公共头文件记录完整契约，实现文件补充算法意图与内部辅助函数，避免用指向自身的 copydoc 形成递归引用。汇编调用桥的寄存器/栈约定写在公共 API 与汇编文件头；Doxygen 从 C 声明提取其 API。

## 注释规则

- 文件使用 file/brief，相关 API 使用 defgroup 分组。
- 函数说明参数方向、返回结果、借用/持有关系、可调用上下文和失败后的状态。
- 宏、枚举和结构体成员使用成员尾注释，持久化字段明确相对于包、正文、记录、区域还是 RAM。
- 不为 void 函数虚构返回值，不添加未经确认的作者、许可证或商业授权声明。
- 修改接口时同时更新教程与失败路径；保留“实现能力”和“实板证据”的区别。
- 原始功能测试仍由 CTest 负责；零 Doxygen 警告不证明代码逻辑正确。

版本维持 V0.0.2，正式许可与商业条款仍见根目录 `LICENSING.md`。文档生成不会发布仓库，也不会给 App 新增权限。
