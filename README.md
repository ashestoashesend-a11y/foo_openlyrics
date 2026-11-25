# foo_openlyrics — fork with AI lyric translation

[中文说明见下方 ⬇](#中文说明)

> **This is a fork of [jacquesh/foo_openlyrics](https://github.com/jacquesh/foo_openlyrics) that adds optional, manually triggered AI lyric translation.**
> Upstream does not have this feature — the maintainer has said [translation is out of scope](https://github.com/jacquesh/foo_openlyrics/issues/170) — so it is maintained here as a standalone fork.
> Everything else is unchanged upstream code, based on tag `v1.14`. The upstream README is kept verbatim as [ORIGINAL-README.md](ORIGINAL-README.md).

## What this fork adds

One thing: **translate lyrics you already have into another language, on demand, with the translation written underneath each original line.**

The design is deliberately restrained:

- **Manual only, never automatic.** Most lyrics need no translation at all, and every request costs tokens, so there is no background translation anywhere.
- **One request per song**, not per line.
- **The translation is written into the lyric file**, so any LRC player shows both the original and the translated line.
- **Re-translating is guarded.** If a translation is already present you are asked first, and only the original lines are sent — an existing translation is never translated as if it were source text.
- **Nothing is saved when the reply is short.** If the response has fewer lines than the request, the whole batch is dropped rather than risking a translation sliding onto the wrong line.

## Setup

`Preferences → Tools → OpenLyrics → Translation`

| Setting | Meaning |
|---|---|
| API endpoint | Any endpoint that accepts OpenAI-compatible `chat/completions` requests |
| API key | Encrypted with Windows DPAPI for the current user. Never stored in plain text, never logged |
| Model | Model name, for example `deepseek-chat` |
| Target language | Free text, for example `Simplified Chinese (简体中文)` |
| Line prefix | Optional prefix for every translated line, for example `译：`. Empty by default |

![Translation settings](.github/readme/translation_settings.png)

After you press Apply the key field clears and the status line reads *"A key is saved, encrypted for this Windows user."* When you change any other setting later, **leave the key field empty** — empty means "keep the saved key".

## Usage

Right-click inside the lyric panel, or a track → `OpenLyrics` → **Translate lyrics (AI)**.

- With no saved lyrics you are told to search for lyrics first.
- With an existing translation you are asked whether to translate again; only the original lines are sent.
- A progress window is shown and can be aborted.

### Storage format

Every translated line sits directly below its original line, sharing the timestamp:

```lrc
[00:28.86]非情な現実に飲み込まれても
[00:28.86]译：即使被无情的现实吞噬
```

Loading that file back merges the pair into a single lyric entry, so the panel highlights the original **and** its translation together. Other LRC players show both lines too, and removing a translation is a matter of deleting the second line.

Translating is **not** treated as a manual edit, so `Uploading → Upload lyrics to LRCLIB = After manual edit` will never publish a machine translation.

### Troubleshooting

Logs go to foobar2000's **View → Console**. `ERROR` and `WARN` are always printed; `INFO` needs verbose logging enabled by placing an empty file named `openlyrics-verbose-log.txt` in `%APPDATA%\foobar2000-v2\`.

```
INFO: Translating 42 lyric lines into Simplified Chinese using deepseek-chat (endpoint: ...)
INFO: AI translation: HTTP status 200 OK
INFO: AI translation: received 1883 bytes
INFO: AI translation: parsed 42 of 42 lines
```

## Install

No prebuilt release is published for this fork. Build it (below) and copy the DLL to:

```
%APPDATA%\foobar2000-v2\user-components-x64\foo_openlyrics\foo_openlyrics.dll
```

Rename the official DLL first if you want an easy way back, e.g. `foo_openlyrics.dll.v1.14-official.bak`.

## Build

Based on upstream tag `v1.14`. Upstream ships no build instructions, so briefly:

1. Visual Studio 2022 (Build Tools are enough) with the **C++ workload, ATL and MFC**, plus a Windows SDK (10.0.26100 used here).
2. Fetch the two libraries that are not in the repo — `curl-8.16.0` and `nghttp2-1.67.1` — using the URLs and SHA256 hashes in `3rdparty/fetch-3rdparty-libs.ps1` (the script's own `tar` step fails under some shells; extracting the tarballs manually works).
3. Build three projects separately, in this order, rather than the `.sln` as a whole:

   ```
   build\cmake-build.vcxproj
   3rdparty\pugixml-1.12.1\scripts\pugixml_vs2022_static.vcxproj
   build\foo_openlyrics.vcxproj
   ```

   With `/p:Configuration=Release /p:Platform=x64 /p:SolutionDir=<repo>\build\` (the trailing backslash matters), `/p:WindowsTargetPlatformVersion=<your SDK>` and `/nodeReuse:false`. A whole-solution build drags in a metrics project that wants the .NET SDK, hence the detour.
4. The result is `build\x64\Release\foo_openlyrics.dll`.

## Known limitations

- Only two layers are supported: original + translation. Lyrics that already are a hand-made bilingual pair are detected as "translated", so re-translating replaces the existing translation.
- The line-by-line protocol requires the model to answer with matching line numbers; a model that ignores the format makes the whole batch fail (nothing partial is written).
- Only tested with foobar2000 v2.x 64-bit, v2.26.

## Syncing with upstream

The fork is one commit on top of upstream `v1.14`. To pick up newer upstream changes:

```
git fetch origin
git rebase origin/main
```

`README.md` is the only file that intentionally diverges from upstream (upstream's copy lives in `ORIGINAL-README.md`), so a rebase may ask you to resolve that one file.

## Screenshots

Fonts and colours are fully configurable, and there is a built-in editor with timestamp support:

![](.github/readme/lyrics_vertical_scroll.gif)

![](.github/readme/editor.jpg)

## License

MIT, copyright © 2021 Jacques Heunis. The fork's additions are released under the same licence.

Upstream project, documentation and donations: [jacquesh/foo_openlyrics](https://github.com/jacquesh/foo_openlyrics).

---

# 中文说明

> **这是 [jacquesh/foo_openlyrics](https://github.com/jacquesh/foo_openlyrics) 的一个 fork，增加了「手动触发、可选的 AI 歌词翻译」。**
> 原版没有这个功能——作者在 [issue #170](https://github.com/jacquesh/foo_openlyrics/issues/170) 中明确说翻译「超出范围」——所以它作为独立分支维护在这里。
> 除新增内容外，其余代码与原版一致，基于原版 tag `v1.14`。原版 README 一字未改地保留在 [ORIGINAL-README.md](ORIGINAL-README.md)。

## 新增了什么

只有一件事：**把已有的歌词按需翻译成另一种语言，译文写在每行原文的下面。**

设计上刻意保持克制：

- **只有手动触发，绝不自动翻译。** 多数歌词本来就不需要翻译，而且每次请求都要花钱，所以没有任何后台自动翻译的入口。
- **一首歌一次请求**，不是逐行请求。
- **译文直接写进歌词文件**，任何 LRC 播放器都能看到原文 + 译文两行。
- **再次翻译有保护。** 识别到已翻译的歌词会先问你，且只把原文发出去——旧译文绝不会被当成原文再翻一次。
- **失败不落盘。** API 返回的行数少于请求行数时整批放弃，绝不保存错位的译文。

## 配置

打开 `Preferences → Tools → OpenLyrics → Translation`：

| 设置项 | 说明 |
|---|---|
| API endpoint | 任何兼容 OpenAI `chat/completions` 的接口地址 |
| API key | 用 Windows DPAPI 按当前用户加密保存，配置文件里没有明文，日志里也不会出现 |
| Model | 模型名，例如 `deepseek-chat` |
| Target language | 想翻译成的语言，自由填写，例如 `Simplified Chinese (简体中文)` |
| Line prefix | 每行译文的前缀，例如 `译：`。默认留空 |

![翻译设置页](.github/readme/translation_settings.png)

点 Apply 后 key 那栏会清空，状态文字变成「A key is saved, encrypted for this Windows user.」——之后修改其它设置时**把 key 留空即可**，留空表示沿用已保存的 key。

## 使用

在歌词面板上右键（或在曲目上右键 → `OpenLyrics`）→ **Translate lyrics (AI)**。

- 没有已保存的歌词时，会提示先去搜索歌词。
- 歌词已经翻译过时，会弹窗询问是否重新翻译；确认后只发送原文。
- 翻译过程中有进度窗口，可以中止。

### 存储格式

译文与原文使用**同一个时间戳**，紧跟在原文下一行：

```lrc
[00:28.86]非情な現実に飲み込まれても
[00:28.86]译：即使被无情的现实吞噬
```

重新加载该文件时，同一时间戳的两行会被合并成一条歌词，所以面板里原文和译文会**同时高亮**，滚动也把它们当作一个整体。换到其它 LRC 播放器同样正常显示两行；不想要译文时，删掉第二行即可还原。

翻译**不算人工编辑**，所以即使打开了 `Uploading → Upload lyrics to LRCLIB = After manual edit`，机翻结果也不会被上传到 LRCLIB。

### 排查问题

日志在 foobar2000 的 **View → Console**。`ERROR` / `WARN` 级别总是输出；`INFO` 级别需要开启 verbose——在 `%APPDATA%\foobar2000-v2\` 下放一个名为 `openlyrics-verbose-log.txt` 的空文件即可。

```
INFO: Translating 42 lyric lines into Simplified Chinese using deepseek-chat (endpoint: ...)
INFO: AI translation: HTTP status 200 OK
INFO: AI translation: received 1883 bytes
INFO: AI translation: parsed 42 of 42 lines
```

## 安装

本 fork 不提供现成的 release 包，需要自行编译（见下），然后把生成的 DLL 放进：

```
%APPDATA%\foobar2000-v2\user-components-x64\foo_openlyrics\foo_openlyrics.dll
```

替换前建议把官方版本的 DLL 改名备份（例如 `foo_openlyrics.dll.v1.14-official.bak`），想回到原版随时换回来。

## 编译

基于原版 tag `v1.14`。原版没有提供编译说明，这里简述实测可用的方式：

1. Visual Studio 2022（Build Tools 即可），需要 **C++ 工作负载 + ATL/MFC**，以及 Windows SDK（本机用的是 `10.0.26100`）。
2. 补齐仓库里没有的两个第三方库：`curl-8.16.0` 与 `nghttp2-1.67.1`。按 `3rdparty/fetch-3rdparty-libs.ps1` 里的 URL 和 SHA256 自行下载并解压到 `3rdparty/`（该脚本自带的 `tar` 步骤在某些 shell 下会失败）。
3. **按顺序**单独编译三个工程，而不是整个 `.sln`：

   ```
   build\cmake-build.vcxproj
   3rdparty\pugixml-1.12.1\scripts\pugixml_vs2022_static.vcxproj
   build\foo_openlyrics.vcxproj
   ```

   共同参数：`/p:Configuration=Release /p:Platform=x64 /p:SolutionDir=<仓库路径>\build\`（末尾反斜杠必须保留）、`/p:WindowsTargetPlatformVersion=<你的 SDK 版本>`、`/nodeReuse:false`。

   直接编译整个 `.sln` 会带上依赖 .NET SDK 的 metrics 工程，因此绕开。

4. 产物为 `build\x64\Release\foo_openlyrics.dll`。

## 已知限制

- 只处理「原文 + 译文」两层；已经是人工双语版的歌词会被识别为「已翻译」，重新翻译会替换掉原有译文。
- 逐行协议要求模型按编号返回，模型不听话时会整批失败（不会写出半成品）。
- 只在 foobar2000 v2.x 64 位、v2.26 上实测过。

## 与原版同步

这个 fork 是在原版 `v1.14` 之上的一次提交。想跟上原版的更新：

```
git fetch origin
git rebase origin/main
```

`README.md` 是唯一有意与原版不同的文件（原版那份在 `ORIGINAL-README.md`），所以变基时可能只需要处理这一个文件。

## 截图

字体和配色都可以调，编辑器支持时间戳：

![](.github/readme/lyrics_vertical_scroll.gif)

![](.github/readme/editor.jpg)

## 许可

MIT 许可，版权归 Jacques Heunis 所有（© 2021）；本 fork 的新增部分同样以 MIT 发布。

原项目、原版文档与捐赠入口：[jacquesh/foo_openlyrics](https://github.com/jacquesh/foo_openlyrics)。
