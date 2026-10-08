# AGENTS.md — 给 AI 助手的项目须知

四色墨水屏恋爱天数牌（ESP32-S3 + 1.54" 微雪 G 屏）。用户-facing 文档见 README.md；
本文件只列**改代码前必须知道**的命令、不变量和历史事故。

## 常用命令

```bash
nix develop                                        # devShell：platformio + python 工具链
nix develop -c pio run -d firmware                 # 固件编译（PlatformIO + Arduino）
nix develop -c pio run -d firmware -t upload       # 烧录（真睡时连不上=正常，按住 BOOT 插 USB 进下载模式）

nix develop -c bash tools/run_host_tests.sh        # 宿主单测（g++ 直编 src 里的纯逻辑文件）

bash tools/flash_watch.sh                          # 挂机烧录（须在 nix develop 里跑）：端口出现 0.3s 内自动烧+首轮对话
bash tools/console_watch.sh "GET" "SLEEP 90"       # 挂机抓控制台：板子一醒就自动发命令
python3 tools/serial_cmd.py --send GET             # 手动控制台（自动断言 DTR）

# 改设计/资产后（devShell 已带渲染依赖与字体）。preview 图的纪念日只从
# MOLLY_ANNIV 环境变量读（缺省占位 2024.01.01），真实日期不进仓库：
nix develop -c python3 tools/prerender.py
```

**监听器纪律**：`console_watch.sh`/`flash_watch.sh` 同一时刻只允许跑一个，启动新
的之前先 `ps aux | grep` 确认旧的已退出（两个同时抢 /dev/ttyACM0 会把对话拆碎，
已发生过）。

## 硬不变量（违反 = 已知事故重演）

1. **几何常量单一来源**：`tools/prerender.py` 生成 `assets/geometry.rs`（Rust 侧）
   和 `firmware/src/assets.h/.cpp`（Arduino 侧）。手改任何一侧的数字坐标 =
   与资产生成脱 sync（曾致徽章/数字错位 4-14px）。
2. **运行时文字基线语义**：`firmware/src/screen.cpp` 的 `draw_text` 复刻
   embedded-graphics 的 Baseline——glyph top = y − offset，Top=0、
   Middle=(height−1)/2、Alphabetic=font.baseline（6x12→9，9x15B→11）。
   字形位图由 `tools/font_extract.py` 从 embedded-graphics 0.8.2 提取
   （`firmware/src/fonts.*` 是生成物），混用锚点语义必错位（曾致两轮对齐 bug）。
3. **补丁替换必须带 assert**。无断言的字符串替换静默 no-op，本项目因此发生过两次
   "提交信息说了但代码没做"的事故。
4. **GPIO17 拉低 = 整板断电**（电池通路自锁，高有效）。固件只允许写高。
   GPIO6/GPIO42 是低有效电源轨（屏/音频），深睡前置高（断电）。
   **睡前 GPIO3/6/17/42 四个 pad 全部 hold，一个不能少**——GPIO17 悬空 =
   睡眠中电池自锁断开 = 整板假死（两个按键、定时唤醒全部失效）。
5. **flash 记录在 0xC000，分区表刻意不认领 0xC000-0xE000**。任何 `erase_flash`
   / `pio run -t erase` 都会抹掉用户数据（纪念日），改分区表前先看
   `firmware/partitions_molly.csv` 头部注释。记录格式（MCV1+FNV1a，
   `firmware/src/store.*`）与 Rust 版字节级兼容，勿动布局。
6. **USB-Serial-JTAG 需要 DTR 断言**才接受主机→设备写入（pyserial：
   `s.dtr = True`；`tools/serial_cmd.py` 已内置）。
7. **PIO 必须在 `nix develop` 里跑**：flake 的 shellHook 把带 intelhex 的
   site-packages 注入 PYTHONPATH（esptool 顶层 import 它），且
   `tool-esptoolpy` 已 pin 到 2.40900.250804（新版包在 nix 下装不上）。若首次
   `pio run` 报 MissingPackageManifestError，**重新进一次 `nix develop`**——
   shellHook 检测到包目录缺 manifest 会自动补写，然后再跑。
8. `reference/` 是引脚/时序/电源的**事实来源**（`waveshare/` 面板驱动、
   `vendor_power/` 电源轨与厂商深睡写法、`schematics/` 原理图）。
   与记忆或猜测冲突时以它为准。**该目录已 gitignore 不入库**（2026-10-08，
   厂商再分发 + 脱敏考虑）：waveshare/ 与 schematics/ 可从微雪 wiki 重新
   下载（链接见 README「移植溯源」）；vendor_power/ 与 design/ 无公开链接、
   仅本机留档，换机器前先备份这两个子目录。

## 架构速览

- `firmware/` — PlatformIO 工程（唯一现役固件）：
  - `src/main.cpp` — 启动流：GPIO17 先写高（电池自锁）→ 解除 pad hold →
    唤醒取证 → 15s 控制台窗口 → 读 RTC → 算天数 → 电池状态机 → 强制全刷 →
    写 flash → 深睡（BOOT ext0 + 06:00 定时器）。
  - `src/{DEV_Config,EPD_1in54g}.*` — 厂商例程拷入（唯一改动：电源轨归 main 管
    （D_9PIN=0，厂商 Module_Exit 会把 GPIO6 写低=开轨）；busy 等待加 LED
    快闪 + 60s 超时）。
  - `src/{civil,battery,store,framebuffer,screen}.*` — 纯逻辑，宿主可测，
    对应 Rust 版同名模块（git 历史即行为规格书）。
  - `src/assets.*`、`src/fonts.*` — 生成物（prerender.py / font_extract.py），
    入库，勿手改。
  - `src/{pcf85063,console}.*` — Wire/Serial 胶水。
  - `partitions_molly.csv` — 自定义分区表（保护 0xC000）。
  - `test_host/` — 宿主单测（g++，无 Arduino 头）。
- `tools/` — prerender.py、font_extract.py、run_host_tests.sh、serial_cmd.py、
  flash_watch.sh、console_watch.sh、render_screen_host.cpp（主机离屏渲染屏面，
  与固件同一份合成代码，拍不清的小字用它对照）。
- Rust 版（`src/*.rs`、`Cargo.*`、`vendor/esp-hal/`）= 行为规格 + 回退路径，
  保留未动；espflash 仍可烧回旧固件（flash 记录两边通用）。
- 日期永远读 PCF85063；天数 = 纪念日起算当天记第 1 天（`date_day_number`）。

## 验证流程（改屏面内容后）

1. `python3 tools/prerender.py` 重新生成两侧资产
2. `pio run -d firmware` + `run_host_tests.sh` 全绿
3. 烧录（flash_watch 挂机，或按住 BOOT 插 USB）
4. 按一次 BOOT 唤醒即可看到最新屏面（固件每次唤醒都强制全刷）
5. `/dev/video0` 摄像头验收：**裁切+摆正+放大到改动区域逐行看**，不要整屏扫一眼
   就下结论（曾漏检 9px 的文字重叠）；拍不清的小字用 render_screen_host 对照
6. 真睡判别：主机 USB 设备干净消失 = 真睡；枚举报 -71 = 半醒挂死

## 其他

- `assets/*.png` 不入库（可由管线再生），唯一例外 `preview_normal.png`
  （README 头图，用占位日期渲染；本机经 MOLLY_ANNIV 出真实日期预览后**勿提交**
  覆盖版）；`*.2bpp`/`*.1bpp`/`geometry.rs`/`firmware/src/{assets,fonts}.*`
  是编译输入，必须入库。
- 设备 USB+电池同时断电会丢 RTC 时间，重上电后需 `SETDATE` 重设。
- 用户判定标准：截图/照片必须放大到具体区域确认，模糊或斜拍不能作为"已验证"。
- 唤醒枚举（IDF 4.4 / S3）：GPIO=1、EXT0=2、EXT1=3、TIMER=4——常见文档的
  0/1/2 序号不适用。
- IDF 4.4 上 ext0+ext1 并存静默失效（enable 返回 OK 但 ext1 永不触发），
  2026-09-25 三次实测 + GET 取证确认；双键唤醒需 Arduino core 3.x/IDF 5.x
  重验，或回退 Rust/esp-hal 的 EXT1 方案。
