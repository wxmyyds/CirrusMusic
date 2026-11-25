# 参与开发

感谢你愿意帮忙。这份文档说明怎么构建、测试，代码大致怎么分层，以及提交时的约定。

## 开发环境

依赖见 [README](README.md#从源码构建)。开发构建打开测试和界面组件预览（gallery）：

```bash
cmake --preset linux-dev            # 用 Qt 官方安装器的 Qt：见 CMakeUserPresets.json.example
cmake --build --preset linux-dev
./build/linux-dev/appCirrusMusic
```

- `appCirrusMusic` 启动后就会连接网易云，并读写 `~/.local/share/Cirrus`、`~/.cache/Cirrus` 和系统钥匙串。测试时请把 `XDG_DATA_HOME`、`XDG_CACHE_HOME`、`XDG_CONFIG_HOME`、`XDG_STATE_HOME` 指到一个固定的目录（不要在 `/tmp` 下，缓存会拒绝其他用户可写的目录），也不要反复用全新的目录启动：网易云会限制新的匿名注册。
- 它是单实例的：已经有一个在运行时，再启动只会激活那一个。
- `appCirrusGallery` 展示主题和全部控件，改界面控件时用它对照；`--grab out.png --page buttons` 可以截图。

## 测试

```bash
QT_QPA_PLATFORM=offscreen dbus-run-session -- \
    ctest --test-dir build/linux-dev --output-on-failure --no-tests=error
cmake --build build/linux-dev --target all_qmllint
```

测试都是离线的：网络请求由假的传输层回放，不会访问网易云。界面没有自动化测试，在 gallery 和真实应用里检查。

## 代码结构

后端是分层的 C++（`src/`），界面是 QML 模块加 C++ 展示层（`ui/`）。

| 目录 | 内容 |
| --- | --- |
| `src/domain` | 纯数据类型和规则：歌曲、歌单、会话、上报……只依赖 Qt Core |
| `src/application` | 用例和状态：账号、查询、播放队列、听歌上报；对外是 `BackendFacade` |
| `src/infrastructure` | 网易云协议与请求、音频引擎和缓存、SQLite 存储、钥匙串、系统媒体控制 |
| `src/logging` | 日志、飞行记录器和诊断包 |
| `ui/presentation` | QML 看到的 `App` 单例和各页面的模型，把后端接到界面 |
| `ui/theme`、`ui/controls`、`ui/components` | 主题（配色、字体、形状、动效）、基础控件、通用组件 |
| `ui/app`、`ui/player`、`ui/window` | 应用外壳和各页面、播放页、窗口和标题栏 |
| `ui/gallery` | 控件预览程序 |
| `tests` | Qt Test 单元测试 |
| `packaging`、`.github/workflows` | 打包和 CI，见 [packaging/README.md](packaging/README.md) |

后端在自己的线程里运行，界面只通过 `BackendFacade` 的信号和命令与它通信。代码注释里引用的 `docs/…` 是维护者的内部笔记，不在这个仓库里。

## 约定

- **格式**：C++ 用仓库里的 `.clang-format`（`clang-format --dry-run --Werror <文件>`）。
- **界面**：产品 QML 只用 `Cirrus.Controls` 的控件，不直接用 `QtQuick.Controls`；颜色、字体、圆角、动效一律取自 `Theme`，不写死；所有操作都要能用键盘完成。
- **日志**：用 `src/logging` 的分类和 `log::debug`／`info`／`warn`／`critical`，一行写完：`log::warn(lcNet, "request.failed", {{"req", id}, {"err", error}})`；事件名（`a.b_c`）和字段名（`lower_snake_case`）在编译时检查。**不要记录** URL、请求或响应内容、凭据和用户输入的文字。
- **生成的文件**不要手改：`ui/theme/Symbols.qml`、`ui/theme/fonts/*.ttf`、`ui/samples/**`、`ui/app/icons/cirrus*`，它们由 `tools/ui/` 下的脚本生成。
- **第三方代码**：不要从 GPL／AGPL 项目复制代码。`third_party/` 里的库保持上游原样。
- **提交**：小而独立的提交，信息用 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/) 格式，例如 `fix(player): ...`、`feat(ui): ...`。用户能感觉到的改动写入 [resources/CHANGELOG.md](resources/CHANGELOG.md) 的当前待发布版本。每次准备正式版时重写此文件，只保留与 `CMakeLists.txt` 版本号一致的一个版本，不追加历史版本或 Unreleased 章节。

## 反馈问题

报告问题时请附上诊断包（设置 › 诊断）。`tools/logs/cirrus_log.py summary <诊断包>` 可以快速查看里面的会话和警告。
