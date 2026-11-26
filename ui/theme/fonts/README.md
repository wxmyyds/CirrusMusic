# 图标字体资源

界面文字默认使用桌面系统字体；用户也可选择已安装的字体或导入自己的字体。Material Symbols Rounded 仅用于图标，经过子集裁剪后随应用资源打包。

修改图标集合时编辑 `symbols.txt`，再运行 `tools/ui/prepare_fonts.py`。脚本从固定的 Google Material Design Icons 提交下载上游资源、校验 SHA-256、生成图标字体子集，并重写 `../Symbols.qml`；不要手工替换生成文件。

| 文件 | 上游 | 版本 | 许可 | 修改 |
| --- | --- | --- | --- | --- |
| `MaterialSymbolsRounded.ttf` | [google/material-design-icons](https://github.com/google/material-design-icons/tree/737e3324305806514d7909874fa1818ae1808232/variablefont) `variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`，提交 `737e3324` | 2.973 | Apache License 2.0，见 `MaterialSymbols-LICENSE` | 用 fontTools 裁剪为 `symbols.txt` 列出的字形并移除 OpenType 布局特性；保留全部可变轴 |

Material Symbols 上游原始文件 SHA-256：`95b24392bb49efd1bc3e92cff4e2452ad094461bab7c97e7d8723fab97e330ca`。

Material Symbols 的子集属于修改后的文件，再分发时须保留本说明与 Apache 2.0 许可文本。
