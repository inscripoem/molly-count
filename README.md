# molly-count

一块 ESP32-S3 四色墨水屏恋爱天数牌：每天早上 6:00 醒来一次，算出"我们在一起"的天数画到屏上，然后深睡。

![preview](assets/preview_normal.png)

## 硬件

微雪 [ESP32-S3-ePaper-1.54G](https://www.waveshare.com/ESP32-S3-ePaper-1.54G)（ESP32-S3-PICO-1-N8R8，1.54" 红黄黑白四色墨水屏 200×200，板载 PCF85063 RTC / SHTC3 / ES8311 / 锂电充电）。

| 外设 | 引脚 |
|---|---|
| EPD 电源轨（低有效） | GPIO6 |
| EPD BUSY / RST / DC / CS | GPIO8 / 9 / 10 / 11 |
| EPD SCLK / MOSI（SPI2） | GPIO12 / 13 |
| RTC PCF85063（I2C0，地址 0x51） | SDA=47, SCL=48 |
| 电池电压（200K/200K 分压，VBAT=2×VADC） | GPIO4（ADC1_CH3） |
| 绿色 LED（低有效） | GPIO3 |
| 音频电源（低有效，未用，睡前置高） | GPIO42 |
| 电池通路自锁（高有效，**勿拉低=关机**） | GPIO17 |

## 构建与烧录

依赖：rustup 的 `esp` 工具链（`espup install --targets esp32s3`）、`espflash`、Nix（devShell 提供 gcc 并注入交叉工具链 PATH）。

```bash
nix develop            # 或装了 nix-direnv 则 cd 即自动进入
cargo build
espflash flash target/xtensa-esp32s3-none-elf/debug/molly-count
```

宿主机单测（纯逻辑：日历、帧缓冲打包、电池状态机、记录格式）：

```bash
cd /tmp && cargo test --manifest-path $PWD/Cargo.toml --lib \
  --no-default-features --target x86_64-unknown-linux-gnu
```

（从项目外运行以绕开 `.cargo/config.toml` 的 xtensa target 与 build-std。）

## 串口控制台

上电后 15 秒内连 USB-CDC（115200）可用命令：

| 命令 | 作用 |
|---|---|
| `SETDATE y-m-d h:m:s` | 设置 RTC（日期+时刻，写入后即时生效） |
| `SETANNIV y-m-d` | 设置纪念日（持久化到 flash） |
| `GET` | 读回纪念日 / 上次显示天数 / 电池电压 / 屏幕种类 |
| `SLEEP <秒>` | 跳过默认睡到 06:00，改为指定秒后唤醒 |
| `BATTTEST LOW\|CRIT` | 演示低电/危急画面（不持久化） |

注意：主机→设备的写入需要 DTR 断言（pyserial 设 `s.dtr = True`）。

## 运行模型

```
06:00 定时唤醒 → 15s 控制台窗口 → 读 RTC → 算天数 + 采样电池
  → 天数或电池状态变化才全刷（~15s）→ 写 flash → 深睡到明早
```

- 日期永远读自 PCF85063（跨深睡走时）；天数 = 纪念日起算、当天记第 1 天。
- 电池阈值（`src/battery.rs`，带滞后）：<3550mV 低电警告、<3350mV 整屏充电警示、恢复需 >3750/3450mV。
- 深睡前切墨水屏电源轨（GPIO6）、关音频轨（GPIO42）、保持电池通路（GPIO17）。

## 设计与资产

屏面为预渲染资产 + 运行时叠加：`tools/prerender.py` 用 Noto Sans CJK 把静态元素
渲染成 2bpp 背景与 80px 数字掩膜，同时**生成 `assets/geometry.rs`**（数字几何常量，
固件 `include!`——管线是唯一事实来源，勿手改）。改版式 = 改 prerender.py →
重跑 → `cargo build` → 烧录。

移植溯源：`reference/waveshare/`（厂商驱动源码，引脚与时序的事实来源）。

## 已知坑

- esp-hal 1.x 需在 `.cargo/config.toml` 手动 `-Tlinkall.x`（链接脚本不再自动注入）。
- e-g `Text` 默认 `Baseline::Alphabetic`（y=基线）；与 PIL 的居中/顶部锚点语义不同，
  混用必错位。运行时文字统一用 `Baseline::Middle`。
- USB-Serial-JTAG 在 DTR 未断言时拒绝主机写入。
- esp-hal 的 build script 对宿主 target 直接拒绝 → 宿主测试用
  `--no-default-features` 剪掉 esp 依赖子树（esp 系依赖均为 optional）。
