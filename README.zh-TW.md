# Listener Firmware

這是執行在 Listener 語音鍵盤上的韌體。它負責麥克風收音，透過藍牙把音訊送給 Listener Type，並讓旋鈕、按鍵、燈、電池和電源管理作為一台完整裝置協同工作。

[English](README.md) · [简体中文](README.zh.md) · [韌體發布](https://github.com/Listener-ai-Macau/Listener-Firmware/releases) · [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type)

<p align="center">
  <img src="docs/assets/readme/keyboard-front.jpg" alt="Listener 語音鍵盤" width="900" />
</p>

## 它在 Listener 裡負責什麼

語音鍵盤和桌面應用程式是同一個產品的兩部分。韌體負責採集和傳送音訊；[Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 負責辨識語音、整理文字，再把結果插入目前游標。

兩邊的分工是明確的。韌體管理裝置時序、傳輸、控制和復原；桌面應用程式判斷喚醒、可選聲紋、自動結束、採用哪份辨識結果、寫作風格和最終輸出。

## 鍵盤裡有哪些能力

- **收音和 BLE 音訊。** PDM 麥克風以 16 kHz 採集，音訊按工作階段分幀、排隊和傳輸，並處理重試、背壓、保留封包重播和尾音保護。
- **旋鈕和按鍵。** EC11 支援單擊、雙擊、長按和旋轉。KEY1–KEY4 支援可設定的單擊、雙擊和長按；Type 不可用時還有安全的 BLE HID 備援動作。
- **看得懂的回饋。** 六組燈顯示電源、藍牙、錄音、處理、成功、警告、充電和韌體升級進度。
- **電池和功耗。** 鍵盤透過 BLE 報告電量，處理充電和低電保護，空閒時暫停音訊工作，可由實體控制喚醒，並支援可調節的休眠與關機時間。
- **設定和復原。** BLE 名稱、燈光亮度、旋鈕動作和功耗時間會保存下來，正常重啟和 OTA 不會清除。普通重連無法解決問題時，還可以重設配對或有線復原。
- **診斷。** 保存在快閃記憶體中的事件日誌重啟後仍然存在，可以透過 BLE 或序列埠匯出。工廠與工程工具覆蓋硬體、音訊、BLE、控制、電源、OTA 和診斷包。

更詳細的實作和驗證入口見[韌體功能對照](docs/features/firmware-feature-map.md)。

## 日常使用

1. 開啟鍵盤，在 Listener Type 的設定 → 裝置中完成配對。
2. 單擊旋鈕開始聽寫，再按一次停止。說完以後，也可以由 Listener Type 自動結束。
3. REC 表示正在收音，AI 表示桌面應用程式正在處理，OK 表示結果已經完成。

旋轉旋鈕可以調節系統音量或螢幕亮度。雙擊會清除藍牙配對並重新進入可發現狀態，長按會關機。KEY1–KEY4 的動作在 Listener Type 中設定。

所有燈熄滅通常表示鍵盤進入了低功耗空閒，下一次受支援的控制操作會將它喚醒。

<p align="center">
  <img src="docs/assets/readme/device-settings.png" alt="Listener 裝置設定" width="720" />
</p>

## 硬體

目前 V2 設定使用 ESP32-S3-WROOM-1-N16R8、16 MB 快閃記憶體和 8 MB Octal PSRAM。裝置包含 PDM 麥克風、帶按壓的 EC11 旋鈕、四顆額外按鍵、六組狀態燈、鋰電池、USB-C 充電和硬體電源保持電路。

BLE 服務包括音訊傳輸、HID、裝置設定、診斷、電量報告和 OTA。語音辨識不在 ESP32-S3 上執行。

## 升級和復原

普通使用者透過 Listener Type 安裝發布版 OTA ZIP。韌體使用兩個應用分割區，會校驗升級包、報告進度，並在新映像無法確認健康啟動時回復。正常升級會保留配對和裝置設定。

USB 燒錄、全擦、序列埠維護和工廠包用於開發、生產或復原。準確的發布檔案和 SHA-256 保存在 [Releases](https://github.com/Listener-ai-Macau/Listener-Firmware/releases)。

## 建置和燒錄

Windows 指令碼會準備專案需要的 ESP-IDF 5.5 環境：

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
pwsh -NoProfile -File .\tools\monitor.ps1 -Port COMx
```

其他 ESP-IDF 命令請透過 `tools\idf.ps1` 執行。

可重用的產品邏輯主要放在 `components/`，ESP32 綁定放在 `ports/esp32/`，共用裝置訊息放在 `protocols/`，啟動組裝放在 `main/`，建置、燒錄、打包、診斷和驗證工具放在 `tools/`。

修改韌體前請閱讀 [CONTRIBUTING.md](CONTRIBUTING.md)，回報裝置問題前請閱讀 [SUPPORT.md](SUPPORT.md)，安全問題請按 [SECURITY.md](SECURITY.md) 私下提交。

目前倉庫還沒有 `LICENSE`，因此能看到原始碼不代表已經取得再散布或修改授權。
