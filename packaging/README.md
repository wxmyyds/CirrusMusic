# 打包与发布

Cirrus 在 Linux 上出 deb、AppImage 和 Arch Linux 原生 pacman 包，Windows 上出安装包和免安装的 zip，macOS 上出 dmg（预览）；另有单独维护的 AUR PKGBUILD。安装包里只有应用本身，没有测试和 gallery。

**所有编译、测试和打包：该操作需要通过 GitHub Actions 执行。** 本文中的脚本说明仅用于理解工作流，不用于本地构建。

## 产物

| 产物 | 文件名 | 内容 |
| --- | --- | --- |
| deb | `cirrus_<版本>_amd64.deb` | 装到 `/opt/cirrus`，自带 Qt；`/usr/bin/cirrus` 是链接；桌面文件、图标、AppStream 装在 `/usr/share` |
| AppImage | `Cirrus-<版本>-x86_64.AppImage` | 和 deb 同一套目录，另外带上基础系统以外的库 |
| pacman | `cirrus-<版本>-1-x86_64.pkg.tar.zst` | 当前检出的提交，装到 `/usr`，依赖 Arch 的 Qt、FFmpeg 和 QtKeychain；Actions artifact 名为 `pacman` |
| AUR | `packaging/aur/PKGBUILD`（`cirrus`） | 独立的发布标签源码配方，不用于当前分支的 pacman 工作流 |
| Windows 安装包 | `Cirrus-<版本>-windows-x64-setup.exe` | Inno Setup；默认只装给当前用户（不需要管理员），也可以选所有用户 |
| Windows 免安装版 | `Cirrus-<版本>-windows-x64.zip` | 解压即用；设置和数据仍在用户目录 |
| macOS（预览） | `Cirrus-<版本>-macos-arm64.dmg` | `Cirrus.app`，自带 Qt 和库，ad-hoc 签名；Apple Silicon，macOS 15 起 |
| 库的源码 | `Cirrus-<版本>-library-sources.tar` | 包里带的 FFmpeg、QtKeychain 的原始源码（LGPL 的源码提供） |
| 校验 | `SHA256SUMS.txt` | |

版本号遵循语义化版本，唯一来源是 `CMakeLists.txt` 里的 `project(CirrusMusic VERSION x.y.z)`；标签是 `vx.y.z`；`CHANGELOG.md` 里每个版本一节，用作 Release 的说明。

## 安装规则

安装规则在 `cmake/CirrusInstall.cmake`。打包时用 `-DBUILD_TESTING=OFF -DCIRRUS_BUILD_GALLERY=OFF`，测试、gallery 和 `tools/` 都不编译。

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `CIRRUS_EXECUTABLE` | `appCirrusMusic` | 可执行文件名；包里 Linux 是 `cirrus`，Windows 是 `Cirrus`，macOS 是 `Cirrus`（`Cirrus.app`） |
| `CIRRUS_DEPLOY_QT` | OFF | 把 Qt 运行库和不在系统目录的库一起装进去（deb、AppImage、Windows、macOS 用，pacman、AUR 不用） |
| `CIRRUS_RUNTIME_DIRS` | 空 | 仅 Windows：去哪里找 FFmpeg、QtKeychain、OpenSSL、zlib、zstd 的 DLL |

Linux 上装的东西：`bin/cirrus`；`share/applications/com.guang233.Cirrus.desktop`；`share/metainfo/com.guang233.Cirrus.metainfo.xml`；`share/icons/hicolor/…/apps/com.guang233.Cirrus.{png,svg}`；`share/doc/cirrus/` 下的第三方声明和许可证。桌面文件和 AppStream 的模板在 `packaging/linux/`。

macOS 上装的是 `Cirrus.app`：包标识 `com.guang233.Cirrus`，图标是构建时用 `iconutil` 从提交的 PNG 生成的 `cirrus.icns`（没有 1024 px 的 PNG，所以缺 `icon_512x512@2x`），第三方声明在 `Contents/Resources/doc/`。

Wayland 下 Dock 图标由合成器按 app ID 从桌面文件里取。开发构建没有桌面文件，用 `tools/ui/install-dev-desktop-entry.sh` 装一个指向开发构建的（装正式包前用 `--remove` 去掉）。

## 随包的库

本节描述 deb、AppImage、Windows 和 macOS 随包部署的库；原生 pacman 包不携带这些库，使用 Arch 仓库的版本。

版本和 SHA-256 固定在 `packaging/deps/versions.sh`。

| 库 | 来源 | 说明 |
| --- | --- | --- |
| Qt 6.11.2 | Qt 官方二进制，模块 qtmultimedia、qtshadertools、qtimageformats | Linux 版要 glibc 2.34 |
| FFmpeg 9.0.2（Linux、macOS） | `packaging/deps/build-ffmpeg.sh` 从源码编译 | 共享库、LGPL，只开 MP3 和 FLAC |
| FFmpeg 9.0.2（Windows） | vcpkg 的 `ffmpeg`（avcodec、avformat、swresample） | 不开 gpl、nonfree |
| QtKeychain 0.17.0 | `packaging/deps/build-qtkeychain.sh`，针对随包的 Qt 编译 | 发行版的 QtKeychain 是针对它们自己的 Qt 编的，不能混用。Windows 上静态链接 |
| OpenSSL 3、zlib、zstd | Linux 用系统的；Windows 用 vcpkg；macOS 的 OpenSSL、zstd 用 Homebrew 的，zlib 用系统的 | Homebrew 的库是为构建机的系统版本编的，所以 macOS 包要 15 起 |

包里有两份 FFmpeg：Qt Multimedia 自带的 FFmpeg 7.1（`libavcodec.so.61`）用于音频输出，Cirrus 的解码器用自己编的 9.0（`libavcodec.so.63`）。大版本不同，库名不同，同时加载不冲突。依赖库的 RUNPATH 是 `$ORIGIN`，在包的 `lib/` 里互相能找到。

## Linux

deb、AppImage 的构建机是 Ubuntu 22.04（glibc 2.35，GCC 12），包能在 Ubuntu 22.04、Debian 12 及更新的系统上运行。通过 `.github/workflows/linux-packages.yml` 调用 `packaging/linux/build-packages.sh`，依赖来自 `packaging/linux/install-build-deps.sh` 和 `packaging/deps/`。`JOBS` 是工作流中的并行编译任务数（默认 4）。

- **deb**：`/opt/cirrus` 自成一体，因为发行版的 Qt 比 6.11 旧。`Depends` 由 `dpkg-shlibdeps` 按包里的程序和库算出，另加 `ca-certificates`（Qt 的 TLS 用系统的根证书）；`Recommends` 一个钥匙串服务。只 strip 还带符号的文件：再 strip 一遍 Qt 已经处理过的库，会弄坏它自带的 `libavutil`。
- **AppImage**：同一套目录装到 `AppDir/usr`，再用 linuxdeploy（固定版本，构建时下载）补上基础系统以外的库并打包。没有自动更新信息。linuxdeploy 会把构建机的 glib 一起带上，所以 `libsecret` 也要显式带上（`--library`）：QtKeychain 在运行时才打开它，linuxdeploy 看不到；不带的话加载的是宿主系统的 libsecret，它要新版 glib 的符号，加载失败，登录状态就存不下来（0.1.0 的问题）。
- **AUR**：`packaging/aur/PKGBUILD` 从 GitHub 的发布标签源码包编译，依赖 Arch 的系统库，`/usr` 布局。目前指向上游 v0.5.0，未上传到 AUR（AUR 暂停注册新账号），不作为本 fork 分支构建的输入；该配方构建的是上游原版，不包含本 fork 的 Bilibili 支持或功能取舍。本 fork 安装包应使用 Actions 的原生 pacman 产物。更新配方后的构建验证也只能通过 GitHub Actions 执行。

## Arch Linux / pacman

`.github/workflows/linux-packages.yml` 的 `pacman` job 在 `archlinux:base-devel` 容器里先完整更新 Arch 并安装原生依赖，不使用 Ubuntu 包、Qt 官方二进制或 `packaging/deps/` 的自编库。工作流创建 `builder` 用户并把工作区交给它；`packaging/arch/build-package.sh` 只允许在 GitHub Actions 中以非 root 身份运行，`makepkg` 不使用 sudo 或自动安装依赖。

- **来源**：对实际检出的 `HEAD` 做 `git archive`（PR 时可为合并提交），以完整提交 SHA 命名源码目录，用 SHA-256 校验源码包。版本从该提交的 `CMakeLists.txt` 的 `project(CirrusMusic VERSION ...)` 提取，填入 `packaging/arch/PKGBUILD.in`；不会下载 AUR 配方的旧发布标签，也不包含工作区未提交的改动。提交、版本和源码校验值写入 Actions 日志。
- **构建依赖**：`base-devel`、`git`、`cmake`、`ninja`、`pkgconf`、`qt6-shadertools`、`qt6-tools`；另外安装下面的运行依赖。Qt 至少 6.10，实际版本取自 Arch 当次仓库。
- **运行依赖**：`gcc-libs`、`glibc`、`qt6-base`、`qt6-declarative`、`qt6-multimedia`、`qt6-multimedia-ffmpeg`、`qt6-wayland`、`qt6-imageformats`、`qtkeychain-qt6`、`ffmpeg`、`openssl`、`zlib`、`zstd`、`hicolor-icon-theme`、`ca-certificates`、`libsecret`。使用系统 Qt/FFmpeg，不把它们复制进包。
- **钥匙串**：`gnome-keyring` 或 `kwallet` 是可选运行依赖；登录凭据持久保存需要可用且已解锁的钥匙串服务及用户 D-Bus 会话。打包和元数据检查不需要账户、凭据或启动钥匙串服务。
- **布局**：`/usr/bin/cirrus`、`/usr/share/applications/`、`/usr/share/metainfo/`、`/usr/share/icons/`、`/usr/share/doc/cirrus/`，另把应用许可证装到 `/usr/share/licenses/cirrus/LICENSE`。`CIRRUS_DEPLOY_QT=OFF`，关闭测试和 gallery。沿用现有 `CIRRUS_PACKAGE=aur` 的发行版渠道标识，不改变应用源码。
- **检查与输出**：工作流用 `pacman -Qip/-Qlp` 检查包元数据和内容，要求版本、可执行文件、桌面文件、AppStream 和许可证存在；只上传一个 `*.pkg.tar.zst`，artifact 名为 `pacman`，Release 自动收集它。不执行应用启动或登录测试。
- **限制**：关闭 LTO（`!lto`），避免 Qt 大字体资源两步 rcc 在 LTO 目标文件中找不到数据；关闭额外调试包（`!debug`）。Cirrus 使用 Qt 的私有 Wayland 客户端接口，Arch 的 Qt 小版本升级后需要在 Actions 中重建包。容器和原生依赖随 Arch 滚动更新，因此同一提交的不同运行不保证字节级可复现；分支包与正式包可能同名、同版本，下载时应核对 Actions 分支和日志中的提交 SHA。

## Windows

MSVC（VS 2022）、Qt `win64_msvc2022_64`、vcpkg（`packaging/windows/vcpkg.json`，固定 baseline），QtKeychain 用同一个脚本在 Git Bash 里编译。安装时 windeployqt 部署 Qt，`CIRRUS_RUNTIME_DIRS` 里的 DLL 和 MSVC 运行库一起放到 `Cirrus.exe` 旁边，不需要另装 vc_redist。安装程序脚本是 `packaging/windows/cirrus.iss`（`AppId` 永远不改）。

安装包没有签名：第一次运行时 SmartScreen 会拦，点“更多信息 → 仍要运行”。

## macOS（预览）

dmg 随 Release 发布，标为预览：只有 Apple Silicon 版，最低 macOS 15（直接用 Homebrew 的 OpenSSL、zstd），ad-hoc 签名、没有公证。

构建机是 GitHub 的 `macos-15`（Apple Silicon），Qt 用官方的 `clang_64`，FFmpeg 和 QtKeychain 用 `packaging/deps/` 的脚本编译（`MACOSX_DEPLOYMENT_TARGET=15.0`），OpenSSL、zstd 来自 Homebrew。

依赖安装、库的编译和 dmg 打包由 `.github/workflows/macos.yml` 调用 `packaging/deps/` 及 `packaging/macos/build-package.sh`，只在 GitHub Actions 中执行。

- 安装时 macdeployqt（`qt_generate_deploy_qml_app_script`）把 Qt 的框架、插件和 QML 模块，以及程序和库用绝对路径引用的非系统库（FFmpeg、QtKeychain、OpenSSL、zstd）复制进 `Contents/Frameworks`，并改写引用。所以两个依赖脚本在 macOS 上保留绝对的 install name（QtKeychain 不用 `@rpath`）。macdeployqt 不认部署脚本的插件列表，打包脚本之后删掉 SQLite 以外的 SQL 驱动。
- 打包脚本检查每个 Mach-O 文件引用的库都在包里或系统里，不能指向构建机的路径；再做 ad-hoc 签名（Apple Silicon 只运行签过名的代码，macdeployqt 改写引用后原来的签名失效），最后用 `hdiutil` 把 `Cirrus.app` 和指向 `/Applications` 的链接打成 dmg。
- 没有公证：第一次打开时系统会拦，用户到 系统设置 › 隐私与安全性 点“仍要打开”，或去掉隔离属性（`xattr -dr com.apple.quarantine /Applications/Cirrus.app`），README 里写了。
- 包只在 CI 上编译和检查过，还没有在真机上充分测试，所以是预览。

## 许可与合规

- Cirrus 本身是 MIT 许可证（根目录 `LICENSE`）。第三方声明在 `packaging/THIRD-PARTY-NOTICES.md`，许可原文在 `packaging/licenses/`，都随包安装（Linux `share/doc/cirrus/`，Windows `licenses\`）。
- 随包部署的 LGPL 库全部动态链接、不加修改，用户可以替换包里的库；Release 附上 FFmpeg、QtKeychain 的原始源码。pacman 包使用 Arch 提供的库，其版本和许可由对应的 Arch 包说明，不适用随包 FFmpeg 的精简配置描述。
- MiSans 字体原样随应用分发，不单独分发（见 `ui/theme/fonts/README.md`）。

## 工作流

| 文件 | 何时 | 做什么 |
| --- | --- | --- |
| `.github/workflows/ci.yml` | 推到 master、PR、手动或被发布调用 | Linux：编译应用、gallery 和测试，qmllint，ctest。手动可加 Windows、macOS；`package=true` 打 deb、AppImage、Windows 和 macOS 的包，不建 Release（macOS 只打包）。不含 Arch 打包任务或 `pacman` 输入；CI 调用 Linux 打包时固定 `format=portable` |
| `.github/workflows/windows.yml` | 被 CI 和发布调用 | Windows 编译；打包时出 zip 和安装包 |
| `.github/workflows/macos.yml` | 被 CI 和发布调用 | macOS 编译和测试；打包时出 dmg |
| `.github/workflows/linux-packages.yml` | 手动、被 CI 和发布调用 | 统一 Linux 打包入口，`format=all`（默认）打 deb、AppImage 和 Arch；`portable` 仅 deb/AppImage 和库源码；`arch` 仅原生 Arch。校验选择值，再由独立 job 构建并上传 `linux` / `pacman` artifact |
| `.github/workflows/release.yml` | 推 `v*` 标签 | 检查标签和版本号、CHANGELOG 一致 → 跑一遍 CI → 通过统一 Linux 入口打 deb、AppImage、Arch，另打 Windows 和 macOS 包 → 全部成功后建一个**草稿** Release，附上全部产物、库的源码和 SHA256SUMS |

仅试 Arch 包时，在 GitHub Actions 选择 **Linux packages → Run workflow**，选择待验证分支，将 **format** 设为 **arch**，完成后下载 `pacman` artifact；该选择不构建 deb/AppImage 或其他平台。CI 不再提供 Arch 开关或任务。Release 使用默认 `all`，无需另起 Arch 工作流。手动入口需要工作流先在默认分支注册，之后可选择功能分支。该操作需要通过 GitHub Actions 执行。

FFmpeg、QtKeychain、vcpkg 的编译结果按脚本和清单的哈希缓存（macOS 的键里还有系统版本下限），Qt 由 install-qt-action 缓存；pacman 工作流不复用这些库的缓存。

## 发一个版本

1. 改 `CMakeLists.txt` 的版本号；把 `CHANGELOG.md` 的 Unreleased 改成 `## [x.y.z]`。
2. 提交，打标签 `vx.y.z`，推送提交和标签。
3. 等发布工作流跑完，在 GitHub 上检查草稿（试装 deb、AppImage、pacman 包、安装包；dmg 能下载、大小正常），再发布。
4. 标签推送后可单独更新 `packaging/aur/PKGBUILD`：改 `pkgver`、`pkgrel` 归 1，更新标签源码包的校验值；后续构建验证需要通过 GitHub Actions 执行，不能在本地打包。原生 pacman 工作流不依赖这一步。
