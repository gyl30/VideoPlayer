# 第三方依赖与内部验证限制

本项目尚未选择许可证。本轮安装包只用于内部验证，不表示已具备对外发行条件。
RPM 的 `NOASSERTION` 表示未作许可证断言，不是许可证授权。

| 组件 | 当前使用方式 | 许可证与需保留的材料 |
| --- | --- | --- |
| Qt Core、Gui、Widgets、OpenGL、OpenGLWidgets、Svg | 动态链接；Windows 同时携带插件 | 核对所用构建的 LGPLv3/GPLv3 选项及组件内第三方许可证，保留替换动态库的能力、对应源码和通知 |
| FFmpeg | 动态链接；Linux 依赖发行版软件包，Windows 携带 MSYS2 DLL | MSYS2 的 9.0.2 构建启用 GPL 和 version3，为 GPL-3.0-or-later；不能按纯 LGPL 构建处理 |
| SDL2 | 动态链接 | zlib 许可证；保留来源与许可证通知 |
| GCC、MinGW 运行库 | Windows 携带非系统 DLL | 核对 GPLv3 与 GCC Runtime Library Exception，以及实际 MinGW 组件的许可证 |
| spdlog、bundled fmt | 项目内置头文件参与编译 | MIT；本包附有原始通知 |
| 其他传递依赖 | Windows 按实际 DLL 闭包收集，Linux 使用系统库 | Windows 应提供 runtime-packages.txt 与 licenses 清单，并逐项核对许可证和对应源码 |

## 发行阻断项

- 没有 VideoPlayer 项目许可证，不能自行把程序标成 GPL、LGPL 或 Proprietary。
- 当前 GPLv3 FFmpeg 与应用组合的对外发行许可尚未解决。
- Windows 库的许可证文件和版本清单不能替代完全对应的源码及构建脚本。
- Qt、FFmpeg 及传递依赖的源码交付、修改说明和可替换/重新链接要求尚需逐项完成。
- 编解码器专利与地区发行条件不由本次技术验证作出结论。

## 官方依据

- [Qt 许可证及 GPL-only 模块](https://doc.qt.io/qt-6/licensing.html)
- [Qt 的 LGPL 义务](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt 内含第三方代码](https://doc.qt.io/qt-6/licenses-used-in-qt.html)
- [FFmpeg 许可证与发行清单](https://ffmpeg.org/legal.html)
- [MSYS2 FFmpeg 构建配方](https://github.com/msys2/MINGW-packages/blob/master/mingw-w64-ffmpeg/PKGBUILD)
- [SDL2 许可证](https://www.libsdl.org/license.php)
- [GCC 运行库许可及例外](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html)
- [spdlog MIT](https://github.com/gabime/spdlog/blob/v1.x/LICENSE)

上游当前网页用于审计说明，不等同于所发行二进制的对应源码存档。
