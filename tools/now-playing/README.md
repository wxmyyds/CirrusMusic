# 播放状态接口（给其他程序）

Cirrus 可以把正在播放的歌、进度和歌词提供给同一用户的其他程序，比如桌面歌词、状态栏（waybar、polybar）、OBS 叠加层。

这个接口有以下特点：

- **只读**：不能控制播放。控制请用 MPRIS（`org.mpris.MediaPlayer2.cirrus`）。
- **只在本机**：走 Unix 套接字，没有网络端口，只接受同一用户的程序。
- **只有 Linux。**
- **默认关闭**：在 设置 › 窗口与系统 › 向其他程序提供播放状态 (私货) 里打开。

只要标题、歌手、进度，MPRIS 就够了。这个接口多出来的是**带时间的歌词**：原文、翻译、罗马音各有逐行和逐字两种，Cirrus 还会告诉你现在唱到哪一行、哪个字。

[`cirrus_now_playing.py`](cirrus_now_playing.py) 是一个只用 Python 标准库的示例客户端：

```sh
python3 cirrus_now_playing.py                          # 每首歌一行标题，每句歌词一行
python3 cirrus_now_playing.py --track translation.standard
python3 cirrus_now_playing.py --once                   # 打印一份快照（JSON）
```

本 fork 沿用上游协议 v1；Bilibili 字幕使用同一个实际播放会话的歌词文档，
源数据只有逐行时间时不会生成逐字时间。`providerSongId` 仍是目录歌曲身份，
不代表实际音频或歌词的提供方；不要据此推断源为网易云。

旧的 `org.cirrus.Lyrics1` 和 `xesam:asText` 导出已移除，不提供兼容接口。

下面是协议 v1。

## 1. 找到 Cirrus

打开设置后，Cirrus 每次启动都会创建：

```text
$XDG_RUNTIME_DIR/com.guang233.Cirrus/          0700
  i-<instanceId>/                              0700
    observation-v1.json                        0600
    s                                          0600（Unix 套接字）
```

- `observation-v1.json` 的内容是 `{"schema":1,"instanceId":"<uuid>","endpoint":"<…/s 的绝对路径>","pid":<进程号>}`。
- 每次启动、每次重新打开设置，`instanceId` 都是新的。Cirrus 退出或关掉设置时会删掉自己的目录；异常退出会留下旧目录，这时连不上它的套接字，跳过即可。
- 请检查：目录和文件都属于自己，不是符号链接，权限是上面列出的值；`endpoint` 就是同一目录下的 `s`；连上以后用 `SO_PEERCRED` 确认对方是同一用户。`pid` 只是提示，不能用来确认身份。

## 2. 帧

每条消息前面是 4 字节的无符号大端长度，后面是这么多字节的 UTF-8 JSON 对象（长度不含这 4 个字节）。请求最长 4 KiB，回复和事件最长 1 MiB。

每条消息都有 `"v":1` 和 `"kind"`，`kind` 取 `request`、`response` 或 `event`：

```json
{"v":1,"kind":"request","id":"r1","method":"getSnapshot"}
{"v":1,"kind":"response","id":"r1","ok":true,"snapshot":{…}}
{"v":1,"kind":"response","id":"r2","ok":false,"error":{"code":"StaleView"}}
{"v":1,"kind":"event","topic":"cursor","snapshot":{…}}
```

请求的格式要求很严：

- `id` 是 1–64 个字母、数字、`_` 或 `-`，在回复之前不能重复使用；
- 不能有多余的字段，也不能有重复的键；
- 整数只能写成十进制整数（不能写 `1.0`、`1e0`）。

不合格的请求会收到 `InvalidRequest`，严重的会被直接断开。回复里可能出现新的字段，忽略它们即可。

## 3. 方法

| 方法 | 字段 | 回复 |
|---|---|---|
| `hello` | — | `instanceId`、`methods`、`tracks`（六轨的名字）、`limits`。**第一条请求必须是它**，5 秒内不发会被断开 |
| `getSnapshot` | — | `snapshot`：当前的完整快照 |
| `subscribe` | `topics`：`snapshot`、`lyrics`、`cursor` 中的 1–3 个 | `topics`、`snapshot`；之后有变化时收到事件 |
| `unsubscribe` | — | 没有别的字段，之后不再收到事件 |
| `getLyricsSlice` | `instanceId`、`viewEpoch`、`trackId`、`lyricsRevision`（都抄自快照）、`track`、`fromLine`、`maxLines`（1–128） | 这一轨从 `fromLine` 起的歌词行 |

- Cirrus 刚启动、还没准备好时，`hello` 会回复 `NotReady`，过一会儿重新连接即可。
- 事件：
  - 每个事件都带一份完整快照，不是增量。几次变化可能合成一个事件，所以 `sequence` 会跳号。
  - 播放时进度最多每秒推 4 次；换歌、暂停、跳转、歌词变化会立刻推。
  - `topic` 是这次变化里最先命中的那一类，按 `snapshot` → `lyrics` → `cursor` 的顺序；同一次变化只推一次。
- 错误码：`UnsupportedVersion`、`InvalidRequest`、`NotReady`、`StaleView`（歌换了或歌词换了）、`LyricsNotReady`、`ExportLimitExceeded`、`Busy`、`RateLimited`。

## 4. 快照

没有的值是 `null`，字段不会省略。计数和 id 是字符串，时间是毫秒整数。

| 字段 | 内容 |
|---|---|
| `instanceId`、`viewEpoch`、`trackId` | UUID。`trackId` 每次加载一首歌都会变（同一首歌重新播放也变），没有歌时为 `null`。`viewEpoch` 在登录账号变化等情况下会变 |
| `sequence` | 递增的计数（字符串） |
| `lyricsRevision` | 这首歌的歌词版本（字符串）：歌词加载好或换了就增加 |
| `availability` | `ready`；`unavailable` 时其他内容都不可信 |
| `playback` | `{state, intent}`。`state` 取 `empty`、`resolving`、`loading`、`ready`、`playing`、`paused`、`ended`、`failed`；`intent` 取 `play`、`pause`、`stop` |
| `track` | `{providerSongId, entryId}`：网易云的歌曲 id，以及它在队列里那一项的 id |
| `metadata` | `{title, artists, album}`，`artists` 是名字的数组；不知道时为 `null`，这时 `metadataExportStatus` 为 `unknown` |
| `timing` | `{positionMs, durationMs, songPositionMs, preview}`。`positionMs` 是在可播放片段里的位置。试听片段时有 `preview: {startMs, endMs}`，`songPositionMs` 是在整首歌里的位置；不是试听时两个位置相同 |
| `clock` | `{sampleMonoMs, positionObservedMonoMs}`：这份快照、这个位置在 Cirrus 里的单调时钟读数（字符串），不是 Unix 时间 |
| `settings` | `{volumeLinear, mode, order, repeat, fmWaiting, fmBlocked}`。`mode` 取 `queue` 或 `personalFm`；`order` 取 `sequential` 或 `shuffle`；`repeat` 取 `off`、`all`、`one`；音量常为 `null` |
| `capabilities` | `canPlay`、`canPause`、`canSeek` 等，仅作参考 |
| `failure` | `null` 或 `{code}`，比如 `Unavailable`（没有版权）、`Network` |
| `lyrics` | 见下一节 |

**在两次事件之间推算进度：** 只在 `playing` 时推算：用 `positionMs` 加上收到快照后经过的时间。超过 1 秒没有收到新的快照，就停止推算。

## 5. 歌词

`lyrics` 的内容是 `{state, documentExportStatus, cursorExportStatus, userOffsetMs, providerNoLyric, instrumental, tracks}`。

- `state`：`none`、`loading`、`ready`、`failed`。
- `providerNoLyric`、`instrumental`：网易云标明没有歌词、纯音乐；不知道时为 `null`。
- `userOffsetMs`：用户在 Cirrus 里给这首歌调的歌词偏移，游标已经算进去了。
- `tracks` 固定有六个键：`original.standard`、`original.enhanced`、`translation.standard`、`translation.enhanced`、`romanization.standard`、`romanization.enhanced`。`standard` 是逐行歌词，`enhanced` 是逐字歌词。每一轨是 `{summary, cursor}`：
  - `summary`：`{availability, timing, providerOffsetMs, malformedLines, lineCount}`。`availability` 取 `missing`、`empty`、`present`、`malformed`；`timing` 取 `plainText`、`lines`、`words`。
  - `cursor`：`{lines: [行号…], words: [{line, word}…]}`，是现在正在唱的行和字。可能同时有几行（重叠的歌词），也可能一行都没有（间奏）。

快照里**没有歌词正文**，正文用 `getLyricsSlice` 取：

```json
{"v":1,"kind":"request","id":"r2","method":"getLyricsSlice",
 "instanceId":"…","viewEpoch":"…","trackId":"…","lyricsRevision":"2",
 "track":"original.enhanced","fromLine":0,"maxLines":128}
```

回复带 `lines`、`lineCount` 和 `nextLine`；`nextLine` 为 `null` 表示已经取完，否则从它继续取。每一行的格式：

```json
{"index":0,"startMs":12340,"endMs":15800,"text":"……",
 "words":[{"index":0,"startMs":12340,"durationMs":420,"text":"…"}]}
```

- `startMs`、`endMs` 可能为 `null`（纯文本歌词，或者最后一行没有结束时间）。
- 时间是在整首歌里的时间，还没有加上 `providerOffsetMs` 和 `userOffsetMs`。所以要自己判断位置时，请用 `songPositionMs − providerOffsetMs − userOffsetMs` 去比较；更简单的做法是直接用快照里的游标。
- 各轨的行号是各自独立的：翻译第 3 行不一定对应原文第 3 行，请按时间或各自的游标去对应。

**歌换了：** `trackId` 或 `lyricsRevision` 变了，就丢掉已取的正文重新取。旧的请求会收到 `StaleView`。只是进度推进或跳转时，正文不会变。

## 6. 限制

`hello` 的 `limits` 里列着各项上限：

- 最多同时 8 个客户端，每个连接最多 4 个未回复的请求；
- 每秒最多 16 帧（允许连续 16 帧的突发）；
- 一帧要在 2 秒内收完；
- 不读数据、积压超过上限，或 5 秒没有读取进展的客户端会被断开。

不要靠断开重连来刷新数据：一个订阅就够了。

## 7. 隐私

打开这个设置，就是允许本用户的所有程序看到你在听什么、听到哪。Cirrus 不会记录哪些程序读了什么，读取也不会让 Cirrus 多发任何网络请求。关掉设置后，目录和套接字立即删除，已连接的客户端会被断开。
