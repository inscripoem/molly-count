# molly-count

一块 ESP32-S3 四色墨水屏恋爱天数牌：每天早上 6:00 醒来一次，算出"我们在一起"的天数画到屏上，然后深睡。

![preview](assets/preview_normal.png)

固件为 **PlatformIO + Arduino**（2026-09 从 Rust/esp-hal 迁移而来，Rust 版保留在 git
历史中作为行为规格与回退路径）。硬件管线照抄厂商例程（`reference/waveshare/`、
`reference/vendor_power/`），应用层逻辑 1:1 移植自 Rust 版，宿主单测同步移植。

## 硬件

微雪 [ESP32-S3-ePaper-1.54G](https://www.waveshare.com/ESP32-S3-ePaper-1.54G)（ESP32-S3-PICO-1-N8R8，1.54" 红黄黑白四色墨水屏 200×200，板载 PCF85063 RTC / SHTC3 / ES8311 / 锂电充电）。

| 外设 | 引脚 |
|---|---|
| EPD 电源轨（低有效） | GPIO6 |
| EPD BUSY / RST / DC / CS | GPIO8 / 9 / 10 / 11 |
| EPD SCLK / MOSI（软件 SPI，照抄厂商例程） | GPIO12 / 13 |
| RTC PCF85063（I2C0，地址 0x51） | SDA=47, SCL=48 |
| 电池电压（200K/200K 分压，VBAT=2×VADC） | GPIO4（ADC1_CH3） |
| 绿色 LED（低有效） | GPIO3 |
| 音频电源（低有效，未用，睡前置高） | GPIO42 |
| 电池通路自锁（高有效，**勿拉低=关机**） | GPIO17 |
| 按键 BOOT / PWR（**均为低有效：按下=接地**） | GPIO0 / GPIO18 |

> **按键极性勘误（2026-09-27，原理图实锤）**：PWR=BAT_KEY 的电路是 **R58 10K
> 上拉到 3V3 + 按下经 D8 肖特基接 GND**——空闲高、按下低。早期任务书写的
> "R33 10K 下拉、按下经 D2 接 VBAT=高" 与实板不符，导致按 PWR 在固件眼里
> 从未存在过（Rust 时代的 "EXT1 唤醒验证" 因此也是误判）。判读硬件时永远
> 以 `reference/schematics/schematic.pdf`（本机留档，不入库）为准。

## 构建与烧录

```bash
nix develop                                        # devShell 提供 platformio + python 工具链
nix develop -c pio run -d firmware                 # 编译
nix develop -c pio run -d firmware -t upload       # 烧录（/dev/ttyACM0，按住 BOOT 插 USB 进下载模式最稳）
```

板子真睡时 USB 设备会消失，esptool 连不上是正常的。 Awake 窗口只有
15s 控制台 + ~20s 刷屏，手速跟不上可用挂机烧录：

```bash
bash tools/flash_watch.sh      # 端口出现 0.3s 内自动烧录，随后自动接首轮控制台对话
```

首次 `pio run` 会联网下载平台与工具链到 `~/.platformio`（约 1.5GB，NixOS 有两个
必踩的坑，见「已知坑」）。

宿主机单测（纯逻辑：日历、帧缓冲打包、电池状态机、记录格式、屏面合成）：

```bash
nix develop -c bash tools/run_host_tests.sh
```

改设计/资产后重跑 `tools/prerender.py`（devShell 已带字体与渲染依赖：
`nix develop -c python3 tools/prerender.py`；非 nix 机器用
`MOLLY_FONT=<Noto Sans CJK 路径>` 指定字体），
它会同时生成 Rust 侧（`assets/*.2bpp`、`assets/geometry.rs`）与固件侧
（`firmware/src/assets.h/.cpp`）的编译输入。运行时文字字形由
`tools/font_extract.py` 从 embedded-graphics 0.8.2 的字体数据提取为
`firmware/src/fonts.h/.cpp`（像素级一致），字体不变无需重跑。preview 图（含
上方头图）里的纪念日读环境变量 `MOLLY_ANNIV`，缺省用占位日期 2024.01.01
（真实日期不进仓库）；本机想看真实预览就加 `MOLLY_ANNIV=<真实日期>` 前缀
重跑，此时不要提交被覆盖的 preview 图。

## 串口控制台

上电后 15 秒内连 USB-CDC（115200）可用命令：

| 命令 | 作用 |
|---|---|
| `SETDATE y-m-d h:m:s` | 设置 RTC（日期+时刻，写入后即时生效） |
| `SETANNIV y-m-d` | 设置纪念日（持久化到 flash 0xC000） |
| `GET` | 读回纪念日 / 上次显示天数 / 电池电压 / 屏幕种类 / 唤醒证据 + 唤醒取证行 |
| `SLEEP <秒>` | 跳过默认睡到 06:00，改为指定秒后唤醒（测试定时唤醒用） |
| `BATTTEST LOW\|CRIT` | 演示低电/危急画面（不持久化） |
| `BTN` | 读回两个按键的当前电平 |
| `WIFI` / `SETWIFI ssid pass` / `FORGETWIFI` | 查看/设置/清除 WiFi 凭证 |
| `SETNTP <host>` | 设置 NTP 服务器（默认 ntp.aliyun.com） |
| `SETOTAURL <url>` | 设置 OTA 基地址（`<url>fw.version` + `<url>molly.bin`），留空禁用 |
| `FORCEOTA` | 本轮唤醒强制检查 OTA（仍需电池 ≥3.6V） |

按键手势（各司其职）：

| 手势 | 效果 |
|---|---|
| 点按 BOOT | 唤醒 |
| 清醒期长按 BOOT ≥5s | 重开配网热点 |
| 清醒期长按 PWR ≥5s | **驻留刷机模式**（LED 常亮、串口在线 30 分钟，`pio run -t upload` 随时可刷；任意键 / `SLEEP` / 超时退出） |
| 复位时按住 BOOT | 下载模式（硬件层兜底） |

注意：PWR 的高电平来自电池，**没电池只插 USB 时 PWR 长按无效**（配网在 BOOT 上，不受影响）。

注意：主机→设备的写入需要 DTR 断言。用项目自带工具最稳：

```bash
nix develop -c python3 tools/serial_cmd.py --send GET      # 自动断言 DTR
```

## 运行模型

```
06:00 定时唤醒 → 15s 控制台窗口 → 读 RTC → 算天数 + 采样电池
  → 强制全刷（~15-20s）→ 写 flash → 深睡到明早 06:00
```

- 日期永远读自 PCF85063（跨深睡走时）；天数 = 纪念日起算、当天记第 1 天。
- **任意唤醒后强制全刷一次**（屏面闪动 = "活着"的观测证据）。
- 电池在位时插拔 USB 不复位、也不会唤醒（VSYS 二极管或 + GPIO17 自锁保持供电），
  唤醒手段只有 BOOT 键和 06:00 定时器。
- 音频轨（GPIO42）全程断电：开机置高、睡前置高 + pad hold，运行期间从不开启。
- 电池阈值（`firmware/src/battery.h`，带滞后）：<3550mV 低电警告、<3350mV 整屏
  充电警示、恢复需 >3750/3450mV。
- flash 记录在扇区 **0xC000**（`MCV1` + FNV1a，32 字节），分区表刻意不把
  0xC000-0xE000 划给任何分区，**重复烧录不清记录**。任何 `erase_flash` 都会
  抹掉它（需 SETANNIV 重设）。
- 深睡前：面板 sleep（0x02+等busy、0x07=0xA5，等 2s，厂商标注 important）→
  EPD 轨断（GPIO6 高）→ 音频轨断（GPIO42 高）→ GPIO17 保持高 →
  **GPIO3/6/17/42 全部 pad-hold**（GPIO17 悬空 = 电池自锁断开 = 睡眠中掉电）。
- 唤醒源：**BOOT（GPIO0 低电平，ext0）+ 06:00 定时器**——出厂语义。PWR
  （GPIO18）只做开机键；IDF 4.4 上 ext0+ext1 并存实测不触发，勿再加回（见已知坑）。
- 绿色 LED 语义：**灭 = 睡眠；慢闪 = 醒着/连网中；快闪 = 面板工作中；三连闪 +
  长灭循环 = 配网等待；常亮 = 驻留刷机模式（PWR 长按触发，端口在线可任意烧录）**。
  没有触发过驻留却见到常亮，仍要当固件卡死或旧固件排查（看端口与串口输出）。
- `GET` 的 `WAKE` 字段（RTC 内存，跨深睡/复位保留、断电清零）：`1`=BOOT/ext0、
  `2`=PWR/ext1（当前固件不会产生）、`0`=断电过或非按键唤醒。定时唤醒不覆盖它，
  按键证据留到下次按键或断电。第二行 `wake_ev:` 输出五路取证
  （框架原因码/裸寄存器/ext1 位图/复位原因/BOOT 引脚电平）。

## 设计与资产

屏面为预渲染资产 + 运行时叠加：`tools/prerender.py` 用 Noto Sans CJK 把静态元素
渲染成 2bpp 背景与数字掩膜，同时生成两侧的编译输入（几何常量是唯一事实来源，
勿手改）。改版式 = 改 prerender.py → 重跑 → `pio run` → 烧录。

移植溯源：`reference/` 目录**不入库**（gitignore，仅本机留档），上游来源——

- 面板驱动与引脚（`waveshare/`）、原理图（`schematics/`）：微雪产品 wiki
  [ESP32-S3-ePaper-1.54G](https://www.waveshare.com/wiki/ESP32-S3-ePaper-1.54G)
  的 Resources 区可下载。
- 电源轨 BSP 与厂商深睡写法（`vendor_power/`，yunliao-s3 例程包）、设计参考图
  （`design/`）：随厂商例程包拷入，无公开下载链接，换机器前自行备份。

## 已知坑

### NixOS + PlatformIO（换机器必看）

- 新版 `tool-esptoolpy`（≥2.41100，2026 起）的注册表包不再自带 `package.json`
  且装后步骤要 pip，nix 的 PIO python 环境没有 pip → `platformio.ini` 固定
  `platformio/tool-esptoolpy@2.40900.250804`。若首次 `pio run` 仍报
  `MissingPackageManifestError`，**退出后重新进一次 `nix develop`**：shellHook
  检测到包目录缺 manifest 会自动补写。（手动兜底：往
  `~/.platformio/packages/tool-esptoolpy/` 写一个最小 package.json。）
- esptool 4.9 顶层 `import intelhex`，nix 的 PIO python 环境没有 → flake 的
  python 环境带了 intelhex，shellHook 把 site-packages 注入 `PYTHONPATH`。
  因此 **必须经 `nix develop` 进 shell 再跑 pio/esptool**；`flash_watch.sh` /
  `console_watch.sh` 同理（脚本会自查依赖，缺了会报错提醒，路径均为仓库内
  相对解析，无机器相关硬编码）。

### 硬件 / 固件

- **IDF 4.4（arduino-esp32 2.0.x）上 ext0+ext1 并存静默失效**：enable 返回
  ESP_OK，但 ext1 硬件从不触发（2026-09-25 三次实测 + GET 取证）。S3 一组
  RTC 唤醒源只能统一电平，PWR(高) 与 BOOT(低) 天生不能同组；S3 的深睡 GPIO
  唤醒源（`esp_deep_sleep_enable_gpio_wakeup`）在本 SDK 未启用
  （`SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP` 未定义）。要双键唤醒需升级
  Arduino core 3.x / IDF 5.x 验证，或回退 Rust/esp-hal 的 EXT1 方案。
- S3 的 `esp_sleep_wakeup_cause` 枚举与常见文档不同：GPIO=1、**EXT0=2**、
  EXT1=3、TIMER=4（多出的 GPIO 项把后续顶了一位）。
- USB-Serial-JTAG 在 DTR 未断言时拒绝主机写入（pyserial 设 `s.dtr = True`，
  `tools/serial_cmd.py` 已内置）。
- GPIO17 拉低 = 整板断电（电池自锁，高有效），固件只允许写高；GPIO6/42 是
  低有效电源轨，睡前置高并 pad-hold。
- Rust/esp-hal 时代的深睡 waiti 缺陷与 USB 睡眠准备缺失（`vendor/esp-hal/`
  补丁）是迁移原因，Arduino/IDF 原生没有这两个问题，**勿把补丁思路搬回来**。
