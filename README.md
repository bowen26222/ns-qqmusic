# ns-qqmusic (Nintendo Switch QQ 音乐后台播放器)

[![Release](https://img.shields.io/badge/Release-v0.1.0-blue.svg)](https://github.com/bowen26222/ns-qqmusic/releases)
[![Platform](https://img.shields.io/badge/Platform-Nintendo%20Switch-red.svg)](https://www.nintendo.com/)
[![Environment](https://img.shields.io/badge/Atmosph%C3%A8re-Supported-brightgreen.svg)](https://github.com/Atmosphere-NX/Atmosphere)
[![License](https://img.shields.io/badge/License-Non--Commercial-orange.svg)](LICENSE)

**ns-qqmusic** 是一款运行于任天堂 Switch（Nintendo Switch）系统上的全功能 QQ 音乐后台播放器。包含系统级后台服务进程（sysmodule `qqmusic.nsp`）与低开销全局覆盖层菜单（Ultrahand / Tesla Overlay `qqmusic-overlay.ovl`），让您在游玩任何 Switch 游戏或在系统主页时，均可在后台自由流式畅听 QQ 音乐在线曲库与本地无损音乐。

---

## 🌟 核心特性

### 1. 完整 QQ 音乐在线生态
- **极速扫码登录**：使用手机 QQ 或 QQ 音乐客户端扫描覆盖层二维码即可一秒安全授权，自动同步账号与权益。
- **全曲库搜索**：内置自研高对比度中文拼音输入法，支持歌曲、专辑、歌手多维度检索。
- **我喜欢的歌**：直连腾讯数据库，服务端倒序同步，最新收藏的歌曲秒级呈现。
- **歌单双源聚合**：**「我创建的自建歌单」与「我收藏的外部歌单」全自动并发拉取与合并去重**，所有歌单 100% 完整展示。
- **收藏的专辑**：双路容灾架构（实时核心接口 + Web 资产接口互为热备），自动跳过失效或版权到期条目，告别打不开和“加载失败”。
- **官方权威榜单**：内置热歌榜、新歌榜、流行指数榜等实时官方热度榜单。
- **心动电台（智能无限流）**：开启电台模式后，当前曲目播完自动预拉取新一波个性化推荐曲目，实现永久不间断的智能推荐流。

### 2. 本地无损曲库与内嵌封面解码
- **全格式硬解码**：原生支持 MP3、FLAC、WAV 无损音频解码与自适应立体声重采样（48kHz / 16-bit 输出）。
- **封面自动落盘**：在线歌曲自动拉取 300×300 专辑原图，本地歌曲自动提取 ID3 APIC / FLAC PICTURE 标签或扫描同目录 `cover.jpg`。
- **双极 Area-Box 降采样**：独创下采样平滑算法与 100% 零内存泄漏管理，在 80×80 像素状态栏盒内丝滑居中绘制。

### 3. 全链路掌机弱网加固与抗抖动
- **针对 Switch WiFi 天线性能弱针对性调优**：
  - 连接建立与 TLS 握手超时放宽至 10 秒；
  - API 网络层针对偶发性丢包提供 300ms 退避重试；
  - 音频流式分段读取 (`HttpBackend`) 配备 **3 次渐进重试**（150ms、300ms、450ms），偶发丢包绝不向解码器误报 EOF 强行截断播放；
  - 静态读缓冲由 256KB 扩容至 **512KB**（约 13 秒音频抗抖动容量），彻底填平 Switch WiFi 偶发断连与握手延迟；
  - 起播防抖：进入新歌若首试失败后台静默重试 2 次；遇到死链或 VIP 限制**自动切到下一首**，只有连续 3 首歌曲失败才判定离线暂停。

### 4. 息屏播放模式（零功耗后台听歌）
- **一键关闭背光**：支持瞬间切断 LCD / OLED 屏幕背光，硬件显示功耗归零，系统维持正常运行，音乐不断流。
- **抑制深度睡眠**：自动抑制系统待机超时；任意点击掌机按键或触摸屏幕瞬间无感亮屏唤醒，并自动复位睡眠策略。

### 5. 高对比度大间距中文拼音输入法
- **大字号与宽间距**：候选栏由 8 候选调整为 **每页 5 候选大字泡**，单字格宽提升至 60px；按键网格边缘间隙放大至 6px，告别误触。
- **深色高对比**：纯黑实体质感按键底框搭配纯白高亮字体（对比度 > 15:1），选中焦点呈现亮青色黑字，在 Switch 掌机小屏幕上字字分明。
- **按键盲选**：数字键 `1~5` 对应候选单字快速上屏，`6~0` 键输入数字，更符合中文输入习惯。

### 6. 游戏独立控制与全局音量管理
- **游戏独立音量**：游戏原生背景音量与音乐播放音量彼此独立、互不干扰。
- **智能游戏黑名单**：可针对每一个游戏独立设置后台音乐开关（如音游启动时自动静音暂停，退出后自动恢复）。

---

## 📦 安装方法

### 运行环境要求
- **系统固件**：Nintendo Switch 全机型（系统版本兼容 12.0.0 ~ 最新 19.x+）
- **引导环境**：Atmosphère 1.0.0+
- **覆盖层前端**：Ultrahand Overlay 或 Tesla-Menu (nx-ovlloader)

### 一键安装
1. 前往本仓库 [Releases 页面](https://github.com/bowen26222/ns-qqmusic/releases) 下载最新的 `ns-qqmusic-vX.X.X.zip`。
2. 将压缩包解压，把得到的 `switch` 和 `atmosphere` 文件夹直接复制并合并覆盖到 **Switch TF (SD) 卡根目录**。
3. 目录层级对应关系如下：
   ```text
   sdmc:/
   ├── atmosphere/
   │   └── contents/
   │       └── 42000000004B4D51/             <-- 后台 sysmodule 服务
   │           ├── exefs.nsp
   │           ├── toolbox.json
   │           └── flags/boot2.flag
   └── switch/
       └── .overlays/
           └── qqmusic-overlay.ovl           <-- 覆盖层插件 (Tesla/Ultrahand)
   ```
4. 重启 Switch 或通过 Hekate / Atmosphere Toolbox 启动 `42000000004B4D51` 后台服务。
5. 在任意界面按下默认覆盖层呼出快捷键（默认：`L` + `D-Pad Down` + `R3 摇杆下按`）即可唤出 QQ 音乐操作面板。

---

## 🎮 操作快捷键

| 操作界面 | 按键 / 手势 | 动作 |
| :--- | :--- | :--- |
| **全局状态栏** | `L` / `R` | 上一曲 / 下一曲 |
| | `ZL` / `ZR` | 按设定步长快退 / 快进（5/10/15/30/60 秒） |
| | `A` | 播放 / 暂停（或激活当前选中的传输按钮） |
| | `D-Pad 左右` | 在循环模式、上一曲、播放、下一曲、随机、进度条间移动焦点 |
| | 触控拖拽 | 直接点击或拖拽进度条跳转播放位置 |
| **曲目与集合列表** | `A` | 播放选中的歌曲 / 点进专辑、歌单或歌手查看曲目 |
| | `X` | 将单曲加入播放队列（不打断当前播放） |
| | `Y` | 队列界面单曲移除 |
| | `B` | 返回上一级菜单 |
| **中文输入法** | `D-Pad 方向` | 在按键网格间移动（首行继续按上进入候选词栏） |
| | `1 ~ 5` 键 | 快速选取对应序号的候选汉字 |
| | `L` / `R` | 候选词栏快速向前 / 向后翻页 |
| | `Y` | 一键切换中英拼音输入模式 |
| | `X` | 字符退格删除 |
| | `+` (Plus) | 输入完成并提交搜索 |

---

## 📂 本地音乐存放规范

- **目录位置**：请将本地音频文件放置在 SD 卡根目录的 `/music` 文件夹下（例如：`sdmc:/music/周杰伦/七里香.flac`）。
- **支持格式**：`.mp3`、`.flac`、`.wav`、`.wave`。
- **本地封面**：支持提取音频文件内嵌的 ID3v2 APIC 标签、FLAC PICTURE 块，或在文件夹中放置 `cover.jpg` / `cover.png` 即可自动呈现封面。

---

## 🛠️ 自行编译构建

本项目使用标准的 **devkitPro / devkitA64** 工具链进行交叉编译。推荐在 WSL (Ubuntu 22.04 / 24.04) 环境下构建：

```bash
# 1. 克隆本仓库及其子模块
git clone --recursive https://github.com/bowen26222/ns-qqmusic.git
cd ns-qqmusic

# 2. 配置 devkitPro 环境变量
source /opt/devkitpro/switchvars.sh

# 3. 全量编译
make -j$(nproc)

# 4. 打包发布 zip（产物生成于 dist/ 目录）
make dist
```

---

## 🧪 自动化回归测试套件

为保证高可靠性与系统稳定性，仓库内置了 5 大在宿主机环境下直接运行的高覆盖率自动化测试套件：

```bash
# 1. 网络层健壮性（参数转义、长数值截断、截断响应防御、传输状态区分）
python3 tools/check-api-robustness.py

# 2. 收藏专辑与歌单多路聚合（Web 接口参数验证、畸形条目容错、自建与收藏歌单合并）
python3 tools/check-fav-albums.py

# 3. 歌曲列表防丢失与游标测试（刷新失败保留旧项、排序后重置首屏、增量翻页游标锁定）
python3 tools/check-songs-refresh.py

# 4. 播放器高并发并发冒烟测试（I/O 阻塞下 IPC 毫秒级响应、防旧数据提交、Seek 合并、按 ID 准确删除）
python3 tools/player-concurrency-smoke.py

# 5. 封面内存防护（连续 500 次切歌、极端分配失败分支、ASan/UBSan 零内存泄漏）
python3 tools/test-cover-memory.py
```

---

## ⚖️ 开源许可证与非商业使用协议 (NC-NA License)

本项目采用 **非商业用途·无需署名·允许二次开发开源许可证 (NC-NA License)** 发布：

1. **允许二次开发**：任何人均可自由修改、扩充、重构本仓库代码，或基于本仓库开展任何形式的二次开发与技术探索。
2. **严格禁止商业化**：**本仓库源码及其任何修改版本、衍生作品或二次开发产物，均严禁以任何直接或间接的形式用于商业目的、付费出售、付费增值服务或商业硬件捆绑。**
3. **无需强制署名**：在严格遵守非商业限制的前提下，任何人在使用、修改或分发本软件时，**无需**在界面或文档中强制保留或展示原作者署名。

完整协议内容请参阅仓库根目录下的 [LICENSE](LICENSE) 文件。
