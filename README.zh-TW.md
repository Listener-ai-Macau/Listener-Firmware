# Listener Firmware

Listener 語音鍵盤的韌體。鍵盤是一台 ESP32-S3 桌面小裝置(16 MB 快閃記憶體、8 MB PSRAM),帶 PDM 麥克風(16 kHz 採音)、一顆能按能轉的 EC11 旋鈕、四顆鍵、六盞狀態燈,鋰電池 USB-C 充電。韌體管收音、按鍵、燈、藍牙配對、省電和 OTA 升級;把語音變成文字,是電腦上 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 的事。

[English](README.md) · [简体中文](README.zh.md) · [1.0.5 版本說明](docs/release/1.0.5.md)

## 從開機到第一段字

1. 充電,按一下旋鈕開機。
2. 開啟電腦上的 Listener Type,點「開始配對」,在 Windows 藍牙裡選 `listener`,回來點「檢查連線」。
3. 游標點進記事本,單擊旋鈕,說話,再單擊。

REC 燈亮就是真在收音;字歸 Type 打進游標。詳細手冊在 Type 倉庫:[語音鍵盤手冊](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)。

## 手上的動作

- 單擊旋鈕開始 / 停止,長按關機,轉動調電腦音量——都能在 Type 裡改。
- 配對亂了,雙擊旋鈕:重置藍牙,重新可被搜尋。
- KEY1–KEY4 的單擊、雙擊、長按各綁一個動作(Type 的設定 → 裝置)。沒設定的鍵走無害的 HID 兜底,不會亂打字。
- 不想伸手:打開「檢測到人聲後自動開始」,裝置先等喚醒詞(預設「開始錄音」),聽到才開錄。

燈是狀態,不是裝飾:PWR 是電源電量,BLE 常亮藍表示桌面端就緒,REC 亮才表示真在採音,AI 亮是傳輸或處理中,OK 是成功或升級中,WARN 有要處理的錯誤。燈全滅通常是休眠省電,不是壞了。亮度上限和低功耗定時都能在 Type 的設定 → 裝置 裡調;電量透過標準藍牙電量服務上報,Windows 裡直接看得到。

## 升級

在 Listener Type 裡 OTA 就行:雙分區,失敗自動回滾,配對和裝置設定都保留。想刷自己改的韌體,用本倉庫腳本走 USB。目前版本 1.0.5,已知問題一個:剛關機後電量讀數可能不準。

## 刷自己的韌體

Windows 下三條命令,ESP-IDF `release/v5.5` 由腳本裝好:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

別裸跑 `idf.py`,走 `tools\idf.ps1`,它先載入 IDF 環境。

## 讀程式碼

`components/` 是產品邏輯,刻意保持跨平台;ESP-IDF 的綁定收在 `ports/esp32/`;`protocols/` 定義編解碼和音訊協議;`tools/` 是建構、燒錄、監控腳本。GPIO 表和驗收工具寫在 [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md)。

貢獻見 [CONTRIBUTING.md](CONTRIBUTING.md);安全問題發 [SECURITY.md](SECURITY.md)。
