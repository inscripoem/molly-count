# molly-count

一款基于 ESP32-S3 的四色墨水屏恋爱天数牌：每日 6:00 定时唤醒，计算"我们在一起"的天数并渲染至墨水屏，随后进入深度睡眠。

![preview](assets/preview_normal.png)

固件采用 PlatformIO + Arduino；早期 Rust/esp-hal 实现仅存于 git 历史。

## 硬件

微雪 [ESP32-S3-ePaper-1.54G](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G)（ESP32-S3-PICO-1-N8R8，1.54" 红黄黑白四色墨水屏 200×200，板载 PCF85063 RTC / SHTC3 / ES8311 / 锂电充电）。

| 外设 | 引脚 |
|---|---|
| EPD 电源轨（低有效） | GPIO6 |
| EPD BUSY / RST / DC / CS | GPIO8 / 9 / 10 / 11 |
| EPD SCLK / MOSI（软件 SPI） | GPIO12 / 13 |
| RTC PCF85063（I2C0，地址 0x51） | SDA=47, SCL=48 |
| 电池电压（200K/200K 分压，VBAT=2×VADC） | GPIO4 |
| 绿色 LED（低有效） | GPIO3 |
| 音频电源（低有效，未使用，睡前置高） | GPIO42 |
| 电池通路自锁（高有效，**拉低即关机**） | GPIO17 |
| 按键 BOOT / PWR（低有效，按下接地） | GPIO0 / GPIO18 |

## 构建与烧录

```bash
nix develop                                        # devShell：platformio + python + 字体
nix develop -c pio run -d firmware                 # 编译
nix develop -c pio run -d firmware -t upload       # 烧录（深度睡眠时无法连接属正常；按住 BOOT 接入 USB 可进入下载模式）
nix develop -c bash tools/run_host_tests.sh        # 宿主单元测试（纯逻辑）
nix develop -c python3 tools/prerender.py          # 修改设计后重新生成资产
```

- 首次 `pio run` 会联网下载平台工具链至 `~/.platformio`（约 1.5GB）。
- 深度睡眠期间 USB 设备会从主机消失，esptool 无法连接属正常现象；唤醒窗口仅有
  约 15 秒串口控制台与 20 秒面板刷新，可使用 `tools/flash_watch.sh` 在端口出现
  后 0.3 秒内自动烧录。
- `prerender.py` 生成固件编译输入 `firmware/src/assets.h/.cpp`（`assets/*.2bpp`
  为中间产物，不入库）。环境变量 `MOLLY_ANNIV=Y.M.D` 用于在本机渲染真实日期
  预览（请勿提交），`MOLLY_FONT` 用于指定非 nix 环境的字体路径。数字区域最多
  支持三位。

## 串口控制台

上电后 15 秒内连接 USB-CDC（115200）。主机至设备的写入需断言 DTR，
`python3 tools/serial_cmd.py --send GET` 已内置该处理：

| 命令 | 作用 |
|---|---|
| `SETDATE y-m-d h:m:s` | 设置 RTC |
| `SETANNIV y-m-d` | 设置纪念日（持久化至 flash 0xC000） |
| `GET` | 读取纪念日 / 天数 / 电池电压 / 唤醒证据 |
| `SLEEP <秒>` | 指定秒数后唤醒（用于测试定时唤醒） |
| `BATTTEST LOW\|CRIT` | 演示低电量/危急画面（不持久化） |
| `BTN` | 读取两个按键电平 |
| `WIFI` / `SETWIFI ssid pass` / `FORGETWIFI` | 查看/设置/清除 WiFi 凭证 |
| `SETNTP <host>` | 设置 NTP 服务器（默认 ntp.aliyun.com） |
| `SETOTAURL <url>` | 设置 OTA 基地址，留空禁用 |
| `FORCEOTA` | 本轮唤醒强制检查 OTA（需电池 ≥3.6V） |

按键手势：

| 手势 | 效果 |
|---|---|
| 点按 BOOT | 唤醒 |
| 唤醒期间长按 BOOT ≥5s | 重新开启配网热点 |
| 唤醒期间长按 PWR ≥5s | 进入驻留刷机模式（LED 常亮、串口保持 30 分钟，可任意烧录） |
| 复位时按住 BOOT | 下载模式（硬件级保底） |

无电池仅接 USB 时 PWR 长按无效（其高电平由电池提供；配网入口在 BOOT 键，不受影响）。

## 运行模型

```
06:00 定时唤醒 → 15s 控制台窗口 → 读取 RTC → 计算天数与采样电池
  → 强制全刷（约 15-20s）→ 写入 flash → 深睡至次日 06:00
```

- 日期读取自 PCF85063（跨深度睡眠走时）；天数自纪念日起算，当天记为第 1 天。
- 任意唤醒后强制全刷一次；装有电池时插拔 USB 不会导致复位或唤醒。
- 电池阈值（`firmware/src/battery.h`，含迟滞）：<3550mV 低电量、<3350mV 充电
  提示，恢复需 >3750/3450mV。
- flash 记录位于 0xC000（32 字节），分区表刻意不将 0xC000-0xE000 划分给任何
  分区：重复烧录不会清除记录，`erase_flash` 则会抹除。
- 深睡前 GPIO3/6/17/42 全部执行 pad-hold（GPIO17 悬空将导致电池自锁断开，
  设备会在睡眠中失电）。
- 唤醒源：BOOT（ext0）与 06:00 定时器；PWR 仅用于开机与运行时手势。
- LED 指示：熄灭=睡眠；慢闪=唤醒/联网中；快闪=面板刷新；三连闪=等待配网；
  常亮=驻留刷机。

## 设计与资产

| 正常 | 低电量 |
|:---:|:---:|
| ![正常](assets/preview_normal.png) | ![低电量](assets/preview_low.png) |
| ![充电](assets/preview_charge.png) | ![配网](assets/preview_setup.png) |

预渲染背景叠加运行时文字与数字；几何常量由 `tools/prerender.py` 单一来源生成。

移植溯源（`reference/` 不入库）：面板驱动与原理图见微雪
[产品文档](https://docs.waveshare.com/ESP32-S3-ePaper-1.54G)（Resources 区）；
电源 BSP 与 RTC 例程取自微雪官方 demo 仓库
[waveshareteam/ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)；
深度睡眠写法参考 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的
yunliao-s3 板级实现；设计参考图无公开链接。
