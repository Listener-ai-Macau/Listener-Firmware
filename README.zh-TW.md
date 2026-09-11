# Listener Firmware

這是 Listener 語音鍵盤上跑的韌體。鍵盤是一台 ESP32-S3 桌面裝置,帶 PDM 麥克風、一顆能按能轉的旋鈕、四顆鍵和六盞狀態燈;韌體管收音、按鍵、燈、藍牙配對、功耗和 OTA 升級。

把語音變成文字的所有環節——辨識、改寫、打進焦點框——都在電腦上的 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 裡。沒有那個軟體,鍵盤只是個守規矩的 BLE 周邊;沒有鍵盤,軟體用任何麥克風都能跑。

[English](README.md) · [简体中文](README.zh.md) · [1.0.5 版本說明](docs/release/1.0.5.md)

### 按鍵

1.0.5 的預設行為:

- 單擊旋鈕開始/停止錄音;雙擊重置藍牙配對;長按關機;轉動調電腦音量。這些都能在 Type 裡改。
- KEY1–KEY4 可以綁單擊、雙擊、長按動作(Type 的設定 → 裝置)。沒設定的鍵走無害的 HID 兜底,不會誤打任何字。
- 在 Type 裡打開「檢測到人聲後自動開始」,鍵盤會先等喚醒詞(預設「開始錄音」),聽到才開錄。

### 燈

PWR 是電源和電量;BLE 常亮藍表示桌面端就緒;REC 亮表示確實在採音;AI 亮表示音訊在傳輸或主機在處理;OK 是成功確認(或升級進行中);WARN 是有要處理的錯誤。燈全滅通常是裝置休眠省電,不是壞了。

### 配對、恢復、升級

配對在軟體裡完成:點「開始配對」,在 Windows 藍牙裡選 `listener`,再點「檢查連線」。配對卡死了去設定 → 關於 → 裝置恢復,用不上串列埠線。

升級走軟體裡的 OTA,配對和設定都保留;用本倉庫 USB 燒錄也行。目前版本是 1.0.5([版本說明](docs/release/1.0.5.md)),已知 bug 是剛關機後電量讀數可能不準。

### 建構和燒錄

腳本只做了 Windows 版,會幫你裝好 ESP-IDF `release/v5.5`:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

不要裸跑 `idf.py`——走 `tools\idf.ps1`,它先載入 IDF 環境。

GPIO 表和其餘工程細節在 [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md)。貢獻見 [CONTRIBUTING.md](CONTRIBUTING.md);安全問題發 [SECURITY.md](SECURITY.md)。
