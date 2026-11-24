<p align="center"><img src="ui/app/icons/cirrus-256.png" width="128" alt="Cirrus"></p>

<p align="center">
  <a href="https://github.com/Guang233/CirrusMusic/stargazers"><img alt="Stars" src="https://img.shields.io/github/stars/Guang233/CirrusMusic?style=flat-square&logo=github" /></a>
  <a href="https://github.com/Guang233/CirrusMusic/releases"><img alt="Release" src="https://img.shields.io/github/v/release/Guang233/CirrusMusic?style=flat-square" /></a>
  <a href="https://github.com/Guang233/CirrusMusic/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/Guang233/CirrusMusic/total?style=flat-square" /></a>
  <a href="https://www.qt.io/"><img alt="Qt" src="https://img.shields.io/badge/Qt-6.10%2B-41CD52?style=flat-square&logo=qt&logoColor=white" /></a>
</p>

# Cirrus

**Cirrus is an unofficial desktop client for NetEase Cloud Music, built with C++ and Qt Quick, made for Linux first.** It streams, caches and plays music with its own gapless audio engine, shows word-by-word lyrics, and keeps your playlists, likes and listening history in sync with your NetEase account. The interface follows Material 3 Expressive and works fully from the keyboard. The app is in Chinese.

---

Cirrus 是一个非官方的网易云音乐桌面客户端，用 C++ 和 Qt Quick 编写，优先面向 Linux。

![首页](packaging/screenshots/home-dark.png)

| 侧边播放器 | 播放页（Material 风格） |
| --- | --- |
| ![侧边播放器](packaging/screenshots/side-player.png) | ![播放页（Material 风格）](packaging/screenshots/player-material.png) |

| 播放页（亮色） | 播放页（暗色） |
| --- | --- |
| ![播放页（亮色）](packaging/screenshots/player-light.png) | ![播放页（暗色）](packaging/screenshots/player-dark.png) |

## 功能

- **发现与搜索**：推荐歌单、歌单分类、排行榜、歌手、新碟，每日推荐与私人 FM；搜索歌曲、歌手、专辑、歌单、播客和声音。
- **音乐库**：创建和收藏的歌单、我喜欢的音乐、收藏的专辑和歌手、最近播放、听歌排行；红心、收藏、关注、新建歌单。
- **播放**：自己的音频引擎，边下边播、无缝衔接、精确跳转，歌曲缓存可设上限；音质选择、音量均衡。
- **播放页**：两种风格：随封面变化的流光背景，或整页取封面颜色的 Material 风格；逐字歌词、翻译和注音，可拖动排序的播放队列。
- **侧边播放器**：悬浮播放条可以停靠到窗口右侧，显示封面、播放控制，以及歌词或接下来的歌曲。
- **听歌记录**：同步到网易云的最近播放、听歌排行和听歌足迹；另有本地的听歌统计看板。
- **桌面集成**：媒体键和系统媒体控制（MPRIS）、托盘、单实例；配色跟随封面或自选主题色，亮色和暗色；有新版本时提示（向 GitHub 查询，可关闭）。
- **登录**：扫码、手机号（验证码或密码）、邮箱；登录状态保存在系统钥匙串里。

## 安装

从 [Releases](https://github.com/Guang233/CirrusMusic/releases) 下载：

| 系统 | 文件 | 安装 |
| --- | --- | --- |
| Ubuntu 22.04 / Debian 12 及更新 | `cirrus_<版本>_amd64.deb` | `sudo apt install ./cirrus_<版本>_amd64.deb` |
| 其他 Linux（x86_64，glibc ≥ 2.35） | `Cirrus-<版本>-x86_64.AppImage` | `chmod +x Cirrus-*.AppImage`，然后直接运行 |
| Windows 10 / 11 | `Cirrus-<版本>-windows-x64-setup.exe` | 运行安装程序；或用免安装的 `Cirrus-<版本>-windows-x64.zip`，解压即用 |
| macOS 15 及更新（Apple Silicon，预览） | `Cirrus-<版本>-macos-arm64.dmg` | 打开 dmg，把 Cirrus 拖进“应用程序” |

- deb 和 AppImage 自带 Qt 和 FFmpeg，不依赖系统里的版本。
- 登录状态保存在系统钥匙串里（GNOME Keyring、KWallet 等）。没有钥匙串服务时也能用，但每次启动都要重新登录。
- Windows 安装包没有签名，第一次运行时 SmartScreen 可能会拦下：点“更多信息 → 仍要运行”。
- macOS 版是预览版，还没有在真机上充分测试，只支持 Apple Silicon。它没有经过 Apple 公证，第一次打开会被系统拦下：到 系统设置 › 隐私与安全性 里点“仍要打开”，或在终端执行 `xattr -dr com.apple.quarantine /Applications/Cirrus.app`。
- Arch Linux：还没有上传到 AUR，可以用仓库里的 [PKGBUILD](packaging/aur/PKGBUILD) 打包安装（用系统的 Qt 从源码编译）：

  ```bash
  git clone https://github.com/Guang233/CirrusMusic.git
  cd CirrusMusic/packaging/aur && makepkg -si
  ```

## 从源码构建

需要：

- CMake 3.24+、Ninja、支持 C++20 的编译器（GCC 12 及更新，或较新的 Clang）
- Qt 6.10+：Core、Gui、Qml、Quick、Multimedia、Network、Sql、DBus、ShaderTools，以及 Wayland 和 Image Formats 模块
- OpenSSL 3、zlib、zstd、FFmpeg（libavcodec、libavformat、libswresample）、[QtKeychain](https://github.com/frankosterfeld/qtkeychain)（Qt 6 版）

Arch Linux 上：

```bash
sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-multimedia qt6-wayland \
    qt6-imageformats qt6-shadertools qtkeychain-qt6 ffmpeg openssl zlib zstd
```

构建并运行：

```bash
cmake --preset linux-release
cmake --build --preset linux-release --target appCirrusMusic
./build/linux-release/appCirrusMusic
```

用 Qt 官方安装器装的 Qt：复制 `CMakeUserPresets.json.example` 为 `CMakeUserPresets.json`，填上 Qt 的路径，改用 `linux-release-local` 预设。开发、测试和代码结构见 [CONTRIBUTING.md](CONTRIBUTING.md)，打包见 [packaging/README.md](packaging/README.md)。

## 数据存放位置

以下是 Linux 上的位置；Windows 和 macOS 上在系统的用户数据目录里。

| 内容 | 位置 |
| --- | --- |
| 设置 | `~/.config/Cirrus/` |
| 播放队列、本地听歌记录 | `~/.local/share/Cirrus/` |
| 封面和歌曲缓存 | `~/.cache/Cirrus/`（可在 设置 › 存储 里清除） |
| 日志 | `~/.local/state/Cirrus/logs/` |
| 登录状态 | 系统钥匙串，名为 `com.guang233.Cirrus` |

## 桌面歌词接口（Linux）

Cirrus 通过标准 MPRIS `Metadata["xesam:asText"]` 提供当前播放歌词的纯文本，
内容来自应用正在显示的原文轨道（包括 Bilibili 字幕）。加载中、无歌词、失败或切歌时
移除该字段，歌词到达后发送 `PropertiesChanged`。不导出 LRC 时间轴、字幕 URL 或账号凭据，
也不为此写入歌词文件。读取该标准字段的桌面应用可直接使用，不需要识别 Cirrus 私有协议。

## 反馈问题

欢迎在 [Issues](https://github.com/Guang233/CirrusMusic/issues) 报告问题。遇到播放失败、加载不出来之类的情况，请在 **设置 › 诊断** 里生成一个诊断包附上：里面有日志和系统信息，不含账号凭据。

## 声明

- Cirrus 是个人开发的第三方客户端，与网易云音乐及网易公司没有任何关系，也未获得其授权或认可。“网易云音乐”是网易公司的商标。
- 音乐、歌词、封面等内容的版权归网易云音乐及各自的权利人所有。Cirrus 不提供、不存储也不分发这些内容，只播放你的账号本来就能播放的歌曲。
- 本项目仅供学习和个人使用，请勿用于商业用途。

## 致谢

- [end-4/dots-hyprland](https://github.com/end-4/dots-hyprland)（illogical-impulse）：界面里的许多控件借鉴了它的设计。
- [NeteaseCloudMusicApiEnhanced/api-enhanced](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced)：本项目的大部分接口来源于此。

## 许可证

[MIT](LICENSE)。安装包附带的第三方软件（Qt、FFmpeg、QtKeychain、MiSans 字体、Material Symbols 等）及其许可证见 [packaging/THIRD-PARTY-NOTICES.md](packaging/THIRD-PARTY-NOTICES.md)。

## Star History

<p align="center">
  <a href="https://star-history.com/#Guang233/CirrusMusic&Date">
    <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=Guang233/CirrusMusic&type=Date" />
  </a>
</p>
