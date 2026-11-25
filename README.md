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

本 fork 保留 Bilibili 辅助音源、实际播放源字幕和安全凭据存储；同步上游 0.5.0 的其他更新，
有意不引入云盘页面、音乐上传、删除及纠错功能。应用内更新仅检查本 fork 的 Release，
避免被上游安装包覆盖；尚未发布的版本从 GitHub Actions 下载测试产物。

![首页](resources/screenshots/home-dark.png)

| 侧边播放器 | 播放页（Material 风格） |
| --- | --- |
| ![侧边播放器](resources/screenshots/side-player.png) | ![播放页（Material 风格）](resources/screenshots/player-material.png) |

| 播放页（亮色） | 播放页（暗色） |
| --- | --- |
| ![播放页（亮色）](resources/screenshots/player-light.png) | ![播放页（暗色）](resources/screenshots/player-dark.png) |

## 功能

- **发现与搜索**：推荐歌单、歌单分类、排行榜、歌手、新碟，每日推荐与私人 FM；搜索歌曲、歌手、专辑、歌单、播客和声音。
- **音乐库**：创建和收藏的歌单、我喜欢的音乐、收藏的专辑和歌手、最近播放、听歌排行；红心、收藏歌单、关注、新建歌单。
- **播放**：自己的音频引擎，边下边播、精确跳转，歌曲缓存可设上限；音质选择、音量均衡；歌曲之间可以无缝衔接，或用智能混音按节拍和调性过渡。
- **播客**：播放播客里的节目和声音，收听记录同步到网易云。
- **播放页**：两种风格：随封面变化的流光背景，或整页取封面颜色的 Material 风格；逐字歌词、翻译和注音，可拖动排序的播放队列。
- **侧边播放器**：悬浮播放条可以停靠到窗口右侧，显示封面、播放控制，以及歌词或接下来的歌曲；播放条上的按钮可以自己选。
- **一起听**：和好友（网易云音乐 App 或 Cirrus）一起听同一份歌单，换歌、暂停、拖动进度互相同步。
- **听歌记录**：同步到网易云的最近播放、听歌排行和听歌足迹；另有本地的听歌统计看板。
- **分享**：复制歌单、专辑、歌手、播客、用户或歌曲的链接，文案可以自己改。
- **桌面集成**：
  - 媒体键和系统媒体控制（MPRIS）、托盘、单实例；
  - 网页上的“在客户端打开”交给 Cirrus 打开（Linux 默认，Windows 安装时可选）；
  - 可以把播放状态和带时间的歌词提供给桌面歌词、状态栏等程序（Linux，默认关闭，见[下文](#给其他程序读取播放状态)）；
  - 配色跟随封面或自选主题色，亮色和暗色；
  - 有新版本时提示（向 GitHub 查询，可关闭）。
- **登录**：扫码、手机号（验证码或密码）、邮箱；登录状态保存在系统钥匙串里。

## 安装

从本 fork 的 [Releases](https://github.com/wxmyyds/CirrusMusic/releases) 下载（尚未发布时使用 Actions 产物）：

| 系统 | 文件 | 安装 |
| --- | --- | --- |
| Arch Linux / Arch Linux ARM | `cirrus-<版本>-1-<x86_64或aarch64>.pkg.tar.zst` | 用 pacman 安装对应架构的包 |
| Debian 13 | `cirrus_<版本>_<amd64或arm64>.deb` | 用 apt 安装对应架构的包 |
| Fedora 44 | `cirrus-<版本>-1.fc44.<x86_64或aarch64>.rpm` | 用 dnf 安装对应架构的包 |
| Windows 10 / 11 x64 | `Cirrus-<版本>-windows-x64-setup.exe` 或 `…-x64.zip` | 运行安装程序，或解压免安装版 |
| Windows 11 22H2 及更新 ARM64 | `Cirrus-<版本>-windows-arm64-setup.exe` 或 `…-arm64.zip` | 运行安装程序，或解压免安装版 |
| macOS 15 及更新（ARM64 / Intel，预览） | `Cirrus-<版本>-macos-<arm64或x86_64>.dmg` | 打开 dmg，把 Cirrus 拖进“应用程序” |

- deb、RPM、Windows 和 macOS 包自带 Qt 和 FFmpeg；Arch 包使用对应发行版的系统库。
- 登录状态保存在系统钥匙串里（GNOME Keyring、KWallet 等）。没有钥匙串服务时也能用，但每次启动都要重新登录。
- Windows 安装包没有签名，第一次运行时 SmartScreen 可能会拦下：点“更多信息 → 仍要运行”。
- macOS 两种架构都是预览版，还没有在真机上充分测试。它没有经过 Apple 公证，第一次打开会被系统拦下：到 系统设置 › 隐私与安全性 里点“仍要打开”，或在终端执行 `xattr -dr com.apple.quarantine /Applications/Cirrus.app`。
- 本 fork 的 Arch 包来自 Actions，并在正式发布时同步到 `arch-cloud-pkgs` 的 cpkg 目录；支持标准 `.pkg.tar.zst` 产物。

## 从源码构建

**该操作需要通过 GitHub Actions 执行。** 在 Actions 选择待构建分支，再运行 `Linux build`、`Windows build` 或 `macOS build`，填写需要构建的源码 `ref`，完成后下载对应架构 artifact。

Linux 构建包含 Arch、Debian、Fedora 的 ARM64 与 x86_64 六个原生发行版 job；Windows 和 macOS 各两个原生架构 job。`CI` 独立执行 Linux、Windows、macOS 测试，不生成安装包。

发布使用 `Release` 工作流：指定源码 `ref` 和版本 `tag`，锁定完整源码提交，并行运行三平台测试和十个原生构建目标，全部成功后统一校验和发布。`publish=false` 可验证完整流程而不发行。细节见 [打包与发布](packaging/README.md)，代码结构见 [CONTRIBUTING.md](CONTRIBUTING.md)。

构建软件包保留 1 天，测试报告保留 3 天，Release 来源清单和校验值保留 7 天。本项目不运行产物清理脚本，旧 Actions 产物由维护者按需手动删除。

## 数据存放位置

以下是 Linux 上的位置；Windows 和 macOS 上在系统的用户数据目录里。

| 内容 | 位置 |
| --- | --- |
| 设置 | `~/.config/Cirrus/` |
| 播放队列、本地听歌记录 | `~/.local/share/Cirrus/` |
| 封面和歌曲缓存 | `~/.cache/Cirrus/`（可在 设置 › 存储 里清除） |
| 日志 | `~/.local/state/Cirrus/logs/` |
| 登录状态 | 系统钥匙串，名为 `com.guang233.Cirrus` |

## 给其他程序读取播放状态

在 Linux 上打开 设置 › 窗口与系统 › 向其他程序提供播放状态 (私货) 后，同一用户的其他程序可以通过本机的 Unix 套接字读取：

- 正在播放的歌（歌名、歌手、专辑）、进度和播放状态；
- 歌词：原文、翻译、罗马音，各有逐行和逐字，还有当前唱到的行和字。

这个开关默认关闭，接口只读，不能控制播放（控制请用 MPRIS），也不会因此多发任何网络请求。
这是本 fork 唯一的详细歌词导出接口：不再导出旧 `org.cirrus.Lyrics1` 或 MPRIS `xesam:asText`。
Bilibili 字幕与界面读取同一个实际播放会话的歌词文档，不伪造逐字时间。
发现清单写在私有运行时目录，歌词正文仅通过 socket 传输；关闭开关会断开客户端并删除自己的端点。
升级后请打开这个新开关，旧的“向外部提供歌词”设置不再使用。
适合用来做桌面歌词、waybar/polybar 的状态栏模块、OBS 叠加层等。协议说明和一个只用 Python 标准库的示例客户端见 [tools/now-playing](tools/now-playing/README.md)：

```bash
python3 tools/now-playing/cirrus_now_playing.py          # 每首歌一行标题，每句歌词一行
python3 tools/now-playing/cirrus_now_playing.py --once   # 打印一份快照（JSON）
```

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
