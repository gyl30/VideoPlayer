# 安装包审计与内部验证

审计基线：`f2b07b102125e9b9cd32293aceba27827b7499cc`，分支 `main`。
本轮仅生成内部验证包，不推送、不发布、不为项目添加许可证，不修改 `third/`。

## 已确认的问题与修改依据

- 原 Windows 只有 ZIP，没有安装、卸载和开始菜单入口。已制作复用原依赖收集脚本的 CPack + WiX 4 草稿和内部 MSI；因图形启动验证未通过，Windows 改动尚未提交。
- 原 DEB 的 ELF 依赖扫描漏掉动态加载的 Qt 平台和 SVG 图标插件。干净 Ubuntu 22.04 使用 `--no-install-recommends` 安装旧 CI 包后，因找不到 `xcb` 插件而退出。现在显式依赖 `qt6-qpa-plugins`，通过 `dpkg-query` 查询 SVG 插件的实际所属包。
- 在 Ubuntu 构建的旧 RPM 依赖 FFmpeg 58，干净 Fedora 43 无法满足。现在每个目标发行版原生构建，保留 `dpkg-shlibdeps` 和 RPM 自动依赖解析；一次 CPack 调用只生成当前系统的原生包，不复制 Linux 共享库。
- 只有西文字体的 Debian 测试环境能启动窗口，但中文标题和按钮显示方框。安装系统 Noto CJK 后截图恢复正常；DEB/RPM 已声明对应系统字体依赖。
- Windows 原脚本只遍历根目录 DLL，且静默跳过 `ldd` 未解析项。草稿已增加插件子目录检查和缺失依赖报错，完整 MSYS2 流程仍待实测。
- 仓库没有项目许可证，RPM 原来的 `Proprietary` 缺乏依据。按维护者决定保留未授权状态，使用 `NOASSERTION`，附内部通知、spdlog/fmt 原始许可及第三方审计说明。

保留现有 Qt + SDL + FFmpeg 架构、播放器源码和动态链接方式。Linux 删除重复 CPack 调用、旧包清理及 Ubuntu 上生成 RPM 的流程，未重写打包器。

## 构建和安装方法

Linux 在目标发行版内安装 Qt6、SDL2、FFmpeg 开发包及生成测试夹具所需的 `ffmpeg` 命令行工具，然后执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
bash scripts/ci/package-linux.sh build dist
```

Ubuntu/Debian 使用 `sudo apt install ./dist/videoplayer_*.deb`；Fedora 使用
`sudo dnf install ./dist/videoplayer-*.rpm`。RHEL 9 系列需要启用 CRB/CodeReady
Builder 和 EPEL，不能用 `rpm --nodeps` 绕过依赖解析。本轮 RHEL 系列实测为
Rocky Linux 9，不能等同于已实测所有 RHEL 版本。

Windows MSI 草稿基于现有 MSYS2 MINGW64 构建；需要 CMake 3.30+、.NET SDK 8、
WiX 4.0.6 及同版本 `WixToolset.UI.wixext`。补丁应用后执行
`scripts/ci/package-windows.sh build dist` 生成 MSI。当前提交中的 Windows 打包仍为 ZIP，未应用未完成改动。
.NET 和 WiX 仅供构建使用，不是播放器运行依赖。
MSI 使用管理员权限安装到 Program Files，提供开始菜单快捷方式和标准卸载入口。

## 运行条件和依赖范围

- Windows 内部 MSI 携带现有 CI 产物中的 Qt、SDL2、FFmpeg、MinGW 和非系统 DLL；已确认加载 `platforms/qwindows.dll` 和 `iconengines/qsvgicon.dll`。草稿计划收集实际包版本、许可证文件和 FFmpeg 构建配置；本轮未实际执行完整 MSYS2 收集流程，不能声称这些新增清单已由新脚本验证。静态检查了 138 个 PE 文件导入；实际启动时的 220 个模块均位于安装目录或 Windows 系统目录。
- Linux 只安装程序、桌面入口、SVG 图标和通知文档，不携带 glibc、Qt、FFmpeg、SDL、Mesa、字体或其他系统库副本。平台插件、字体和共享库均由包管理器提供。
- 需要可用的桌面图形会话和能够建立 OpenGL 2.1 / GLSL 1.20 上下文的驱动。Linux 容器验证使用 Xvfb 和系统 Mesa 软件渲染，不代表在没有显示服务器或驱动的系统上也能播放。
- 容器连接宿主 Xvfb 时设置 `QT_X11_NO_MITSHM=1`，避免不同 IPC 命名空间的共享内存导致截图全黑；这是测试环境设置，不加入产品启动逻辑。
- 中文界面需要系统 CJK 字体；Windows 使用系统字体，Linux 由包依赖安装 Noto CJK。
- 编解码能力取决于系统 FFmpeg。Fedora/Rocky 本轮库提供 OpenH264 解码器，未提供 HEVC 解码器；没有为扩展编解码能力启用 RPM Fusion 或捆绑另一套库。
- 卸载移除程序、插件、快捷方式/桌面入口和随包文档，保留用户播放列表、设置及日志。共享系统依赖仍由包管理器管理，不删除其他包拥有的目录。

## 验证记录

本地证据目录：`/tmp/videoplayer-packaging-20261025`。
构建容器与运行容器分开；运行容器没有 Qt/FFmpeg/SDL 开发包。
四种 Linux 包均通过包管理器解析依赖后安装，未使用 `--nodeps`。

| 包和目标 | 安装 | 图形启动 | 正常退出 | 卸载及安装文件残留 |
| --- | --- | --- | --- | --- |
| DEB / Ubuntu 22.04 amd64 | 通过 | 通过，中文及 SVG 正常 | 通过 | 通过 |
| DEB / Debian 12 amd64 | 通过 | 通过，中文及 SVG 正常 | 通过 | 通过 |
| RPM / Fedora 43 x86_64 | 通过 | 通过，中文及 SVG 正常 | 通过 | 通过 |
| RPM / Rocky Linux 9 x86_64 | 通过 | 通过，中文及 SVG 正常 | 通过 | 通过 |
| MSI 草稿 / Windows 10 Pro 22H2 x64 | 通过，管理员安装 | 失败，缺软件 OpenGL 库 | 通过 | 通过 |

Linux 验证检查真实的 1080×680 窗口、Qt 插件加载及
`video widget shader linked successfully` 日志，再发送 `WM_DELETE_WINDOW`，
检查退出码、包注册和程序/桌面入口/图标/文档是否移除。
Rocky 的 `/usr/lib/.build-id` 由其他系统包共同拥有，卸载后保留正常；本程序的 build-id 链接已移除。
窗口截图和 `*-final-launch.log`、`*-final-exit.txt`、`*-final-uninstall.log`
保留在证据目录。没有把仅打包成功或进程短期存活当作完整安装验证。

Windows 测试只操作既有 Win10 的写时复制测试盘；验证期间基础磁盘只读挂载，未写入原盘，也未启动或关闭原虚拟机。
使用新建专用账户和独立空白工作盘。该环境是隔离克隆，不是全新安装的 Windows。
草稿测试脚本仅给播放器子进程保留 Windows 系统 PATH，检查实际加载模块的路径，
防止构建目录或 Qt SDK 隐式补足安装包依赖。

Windows 图形失败的实际证据：窗口标题和可见标志正常，矩形为 `(259,89)-(1339,769)`，但截图没有播放器界面。
`QT_FORCE_STDERR_LOGGING=1` 和 `qt.qpa.gl=true` 日志显示系统仅提供 OpenGL 1.1，
Qt 自动尝试 `opengl32sw.dll` 后因该库不存在而失败，继而无法创建 QRhi 和 QOpenGLWidget 上下文。
没有把“句柄存在、进程存活”计为图形启动成功。草稿已增加图形初始化错误检查；新回归检查尚未完整复测。

后续外部操作启动了原有 `win10`，基础镜像被锁定。按维护者选择，本轮停止 Windows 复测并交付已完成部分；
未关闭原有虚拟机，未强行解锁磁盘。原基础盘随后可能变化，现有写时复制测试盘不作为后续验证依据。
Mesa 及其依赖仅下载核验，尚未加入项目、修改安装包或实施图形修复。

内部 MSI 使用基线 CI 二进制及原部署树。为本地封装，临时安装项目复用仓库的安装/CPack 段，
以安装预编译文件代替重编目标。因此没有证明当前主项目的完整 Windows 构建和新依赖收集脚本已通过。
Windows 全新环境安装、可见界面和软件 OpenGL 修复仍未完成。

未实测 Ubuntu 24.04 arm64、原生 RHEL、Wayland、Windows 11 和 Windows 硬件图形驱动；
对应 CI 配置不等于这些环境已经通过。macOS 打包保持原样，不属于本轮交付。

## 测试结果与限制

- 本机现有 CTest：2/2 通过；ASan + UBSan 构建 CTest：2/2 通过。
- Ubuntu 22.04、Debian 12、Fedora 43、Rocky Linux 9 的原生构建完整 CTest，单独运行均为 2/2 通过，耗时约 37–38 秒。
- 四个容器同时执行全量测试曾触发既有 60 秒超时；慢放用例单独执行及各环境逐一全量复验均通过。未确定并行超时的根因，未修改播放器或扩大超时。
- 原生构建测试最初缺少生成夹具所需的 `ffmpeg` 命令行工具，CI 的构建依赖已补齐；Linux 安装包不增加该命令行工具的运行依赖。
- ASan/UBSan 的真实 X11 `window_interaction` 功能断言 3/3 通过，但退出时 LeakSanitizer 报告来自 GLib `g_malloc` 的 56 字节分配未释放，进程退出码为 1。改用通用 Qt 主题后仍存在，来源未定位，不能声明图形路径泄漏检查完全通过。没有修改第三方代码或屏蔽泄漏报告。
- `bash -n`、ShellCheck warning 级别及 `git diff --check` 通过；另有一轮只读独立审查，未发现确定的配置缺陷。

## 许可证与发行阻断

详见 [第三方通知](../packaging/THIRD-PARTY-NOTICES.md)。MSYS2 当前 FFmpeg
9.0.2 启用 GPL 和 version3；不能按纯 LGPL 处理。版本清单和许可证文本不能替代
完全对应源码、构建脚本及必要的重新链接/库替换能力。项目本身没有许可证，
本轮验证不解除这些阻断项，也不授权公开发行。

## 官方技术依据

- [CPack WiX 生成器及 WiX 4 配置](https://cmake.org/cmake/help/latest/cpack_gen/wix.html)
- [WiX 扩展缓存机制](https://docs.firegiant.com/wix/development/wips/6184-command-line-extension-acquisition-and-cache/)
- [Qt Windows 部署](https://doc.qt.io/qt-6/windows-deployment.html)
- [Qt Linux/X11 要求](https://doc.qt.io/qt-6/linux-requirements.html)
- [CPack DEB 自动共享库依赖](https://cmake.org/cmake/help/latest/cpack_gen/deb.html)
- [CPack RPM 原生依赖配置](https://cmake.org/cmake/help/latest/cpack_gen/rpm.html)
- [Qt Windows 图形加速及软件 OpenGL](https://doc.qt.io/qt-6/windows-graphics.html)

## 本地交付和续接

- 已完成提交：`b54e7c4`（内部通知与许可审计）、`ea4662a`（Linux 原生包与依赖修复）。
- 内部包目录：`dist/internal-20261026/`；包含两个 DEB、两个 RPM、一个标为 `INCOMPLETE` 的 MSI 及 `SHA256SUMS`。
- Windows 未完成改动：同目录 `windows-msi-wip.patch`，已用 `git apply --check` 检查可应用性，未提交到 main。
- Linux 最终包只恢复了已验证的公共通知文本，二进制、桌面入口、图标及依赖保持不变，未为文档变化重复安装闭环。
- 本轮未推送、未发布 Release。现有 tag 自动发布配置未触发；发行阻断解除前不能据此公开发布。

本轮创建的九个测试容器及 Xvfb 已停止；保留产物、日志和缓存供核验，未停止其他容器或原有 win10。
