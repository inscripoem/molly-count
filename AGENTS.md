# AGENTS.md — 给 AI 助手的项目须知

四色墨水屏恋爱天数牌（ESP32-S3 + 1.54" 微雪 G 屏）。用户-facing 文档见 README.md；
本文件只列**改代码前必须知道**的命令、不变量和历史事故。

## 常用命令

```bash
nix develop                 # 必须先进 devShell：宿主链接器 gcc + esp 交叉工具链 PATH
cargo build                 # xtensa 固件（target/config 已在 .cargo/config.toml）
espflash flash target/xtensa-esp32s3-none-elf/debug/molly-count

# 宿主机单测（必须在项目目录外运行，见下）
cd /tmp && cargo test --manifest-path /home/inscripoem/dev/molly-count/Cargo.toml \
  --lib --no-default-features --target x86_64-unknown-linux-gnu

# 改设计/资产后
python3 tools/prerender.py  # 需 noto-fonts-cjk-sans + numpy + pillow（nix-shell -p ...）
```

## 硬不变量（违反 = 已知事故重演）

1. **几何常量单一来源**：`assets/geometry.rs` 由 `tools/prerender.py` 生成，`src/assets.rs`
   `include!` 它。手改固件侧数字坐标 = 与资产生成脱 sync（曾致徽章/数字错位 4-14px）。
2. **e-g `Text` 默认 `Baseline::Alphabetic`**：position.y 是**基线**不是顶部。
   PIL 的 `lm` 锚点是垂直居中——两边坐标语义不同，混用必错位（曾致两轮对齐 bug）。
   运行时文字统一 `Text::with_baseline(..., Baseline::Middle)`。
3. **补丁替换必须带 assert**。无断言的字符串替换静默 no-op，本项目因此发生过两次
   "提交信息说了但代码没做"的事故。
4. **GPIO17 拉低 = 整板断电**（电池通路自锁，高有效）。固件只允许写高。
   GPIO6/GPIO42 是低有效电源轨（屏/音频），深睡前置高（断电）。
5. **esp-hal 的 build script 对宿主 target 直接 panic**。所有 esp 系依赖挂在
   `hw` feature（default）下；宿主测试必须 `--no-default-features`，且因
   `.cargo/config.toml` 的 build-std 对宿主 target 无效，测试要从项目目录外运行。
6. **USB-Serial-JTAG 需要 DTR 断言**才接受主机→设备写入（pyserial：`s.dtr = True`）。
7. NixOS：cargo 源码编译（build script / cargo install）需要 gcc，进 devShell 即有；
   不要试图改系统环境。
8. `reference/waveshare/` 是引脚/时序的**事实来源**（厂商驱动源码）。与记忆或猜测冲突时以它为准。

## 架构速览

- `src/main.rs` — 启动流：15s 控制台窗口 → NVS 读记录 → RTC → 电池状态机 →
  条件全刷 → 深睡（GPIO17 保持、GPIO42/6 断电、定时器唤醒至 06:00）。
- `src/lib.rs`（宿主可测，无 esp 依赖）：`civil` 日历数学、`framebuffer` 2bpp 打包、
  `battery` 滞后状态机、`store` flash 记录（扇区 0xC000，magic MCV1 + FNV1a，
  screen_kind 字节在校验和区外=向后兼容）、`assets`（include_bytes 资产 + 数字叠加）。
- `src/{epd1in54g,pcf85063,console}.rs` — esp-hal 胶水（bin 专用）。
- 日期永远读 PCF85063；天数 = 纪念日起算当天记第 1 天（`day_number`）。

## 验证流程（改屏面内容后）

1. `python3 tools/prerender.py` 重新生成 2bpp/1bpp/geometry.rs
2. `cargo build` + 宿主测试
3. `espflash flash ...`
4. 面板不会自动重刷（天数未变则跳过）→ SETDATE 往返（+1 天再回来）强制两次刷新
5. `/dev/video0` 摄像头验收：**放大到改动区域逐行看**，不要整屏扫一眼就下结论
   （曾漏检 9px 的文字重叠）

## 其他

- `assets/*.png` 不入库（可由管线再生）；`*.2bpp`/`*.1bpp`/`geometry.rs` 是编译输入，必须入库。
- 设备 USB+电池同时断电会丢 RTC 时间，重上电后需 `SETDATE` 重设。
- 用户判定标准：截图/照片必须放大到具体区域确认，模糊或斜拍不能作为"已验证"。
