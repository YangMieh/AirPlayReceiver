# AirPlay Receiver for Windows — iOS Screen Mirroring (60fps, high-res)

> A Windows AirPlay receiver that lets an **iPhone/iPad mirror its screen to a PC with no app installed on the phone** — including watching **YouTube together** at **60fps** and **super‑sampled high resolution**.
>
> 讓 **iPhone/iPad 不裝任何 app**，用內建「螢幕鏡像」把畫面投到 Windows PC 的 AirPlay 接收器 —— 可以**一起看 YouTube**，支援 **60fps** 與**超採樣高解析度**。

**This project is a fork of / built on [xenos1337/AirPlayServer](https://github.com/xenos1337/AirPlayServer) (MIT), which itself builds on [fingergit/airplay2-win](https://github.com/fingergit/airplay2-win). Huge thanks to both — their work is the foundation that made all of this possible.**

**本專案基於 [xenos1337/AirPlayServer](https://github.com/xenos1337/AirPlayServer)（MIT 授權）延伸開發，而它又源自 [fingergit/airplay2-win](https://github.com/fingergit/airplay2-win)。核心的 AirPlay 連線與解碼都來自它們，由衷感謝！**

> For personal / non‑commercial use. 僅供個人／非商業用途。

---

## English

### What this fork adds
The upstream receiver connects on modern iOS but had a few walls for real‑world use. This fork adds:

- **Watch YouTube (and other cast apps) via mirroring** — apps like YouTube auto‑switch to *AirPlay Video* mode (which the receiver can't play) and go blank. We hide the receiver's "video" capability in the mDNS advertisement so the app falls back to **screen mirroring**, and you see it normally.
- **60fps** — we patch the advertised display `maxFPS` (30 → 60) so the iPhone streams at 60fps.
- **High‑resolution super‑sampling** — we bump the advertised display resolution so the iPhone sends a higher‑res mirror; downscaled into the window it looks noticeably crisper (like watching 4K content on a 1080p screen).
- **No green screen** — auto‑selects a working GPU render backend (OpenGL) instead of the buggy D3D11 YUV path.
- **No more crashes** — the video‑mode transition made the bundled DLL kill the whole process; we intercept `NtTerminateProcess` so the app survives and keeps mirroring.
- **Quality‑of‑life UI** — a top toolbar (revealed on top‑edge hover) with Info / Fullscreen buttons, a mouse‑closable info panel, and **instant borderless fullscreen** (no black flash).
- **Hotkeys always work** — IME is disabled for the window so `H` / `F` work in any input language (no need to switch off Zhuyin/CJK input).
- Everything adapts to whatever screen it runs on, so it's fine to copy the folder to another PC or share with friends.

### Requirements
- Windows 10/11 (x64)
- **Apple Bonjour** (for device discovery) — install it from Apple's official page:
  [**support.apple.com/106380**](https://support.apple.com/106380) (auto‑localizes to your region).
  *If it's missing, the app opens this link for you automatically on startup.*
- An iPhone/iPad on the same Wi‑Fi network

### Build
Open `AirPlay.sln` in Visual Studio 2022 (Desktop C++ workload), or from a *x64 Native Tools Command Prompt*:
```
MSBuild AirPlayServer\AirPlayServer.vcxproj /p:SolutionDir=<repo>\ /p:Configuration=Release /p:Platform=x64
```
Output: `x64\Release\AirPlayServer.exe` (the runtime DLLs live next to it).

### Usage
1. Run `AirPlayServer.exe`.
2. On the iPhone: Control Center → **Screen Mirroring** → pick the receiver.
3. For sharpest video, watch fullscreen in landscape.
4. Move the mouse to the **top edge** for the toolbar; press **H** for the info panel, **F** for fullscreen.

### Known limitations
- **Netflix / DRM content won't play** — this is a hardware/OS DRM wall (FairPlay), impossible for any homemade receiver. Watch Netflix directly on the PC instead.
- YouTube's on‑screen controls occasionally get stuck (rare; reopen to clear).
- Very high resolution uses a lot of bandwidth; on weak Wi‑Fi you may see occasional frame drops.

---

## 繁體中文

### 這個 fork 加了什麼
上游接收器能在新版 iOS 連線，但實際使用有幾道牆。這個版本補上：

- **用「鏡像」看 YouTube（及其他投放 app）** —— YouTube 之類的 app 一偵測到 AirPlay 就自動切成「AirPlay 影片模式」（接收器播不了）而變空白。我們在 mDNS 廣播中**藏起「影片」能力**，逼 app 退回**螢幕鏡像**，畫面就正常顯示。
- **60fps** —— 攔改接收器宣告的 `maxFPS`（30 → 60），讓 iPhone 以 60fps 串流。
- **高解析超採樣** —— 把宣告的顯示解析度改大，iPhone 就送更高解析的鏡像；縮小塞進視窗後明顯更銳利（就像在 1080p 螢幕上看 4K 內容）。
- **消除綠畫面** —— 自動挑選能正常運作的 GPU 渲染後端（OpenGL），避開有 bug 的 D3D11 YUV 路徑。
- **不再閃退** —— 影片模式切換時，內建 DLL 會把整個程式關掉；我們攔截 `NtTerminateProcess`，讓程式存活、繼續鏡像。
- **好用的介面** —— 頂端工具列（滑鼠靠近上緣才浮現）含 Info／Fullscreen 按鈕、資訊面板可用滑鼠關閉、**無邊框全螢幕秒切**（不黑屏）。
- **熱鍵永遠有效** —— 停用視窗 IME，`H`／`F` 在任何輸入法下都能按（不用關掉注音）。
- 所有設定會依執行當下的螢幕自動適應，整包複製到別台電腦或分享給朋友都能直接用。

### 需求
- Windows 10/11（x64）
- **Apple Bonjour**（裝置探索用）—— 從 Apple 官方頁面安裝：
  [**support.apple.com/106380**](https://support.apple.com/106380)（會自動導到你的語言）。
  *若沒裝，程式啟動時會自動幫你開這連結。*
- 與 PC 同一 Wi‑Fi 的 iPhone/iPad

### 建置
用 Visual Studio 2022（桌面 C++ 工作負載）開 `AirPlay.sln`，或在「x64 Native Tools 命令提示字元」執行：
```
MSBuild AirPlayServer\AirPlayServer.vcxproj /p:SolutionDir=<repo>\ /p:Configuration=Release /p:Platform=x64
```
輸出：`x64\Release\AirPlayServer.exe`（執行期 DLL 就在旁邊）。

### 使用
1. 執行 `AirPlayServer.exe`。
2. iPhone：控制中心 →「**螢幕鏡像**」→ 選這台接收器。
3. 想要最清晰的影片，把手機轉橫、影片全螢幕。
4. 滑鼠移到**視窗上緣**叫出工具列；按 **H** 開資訊面板、**F** 全螢幕。

### 已知限制
- **Netflix／DRM 內容無法播放** —— 這是硬體/系統層級的 DRM 硬牆（FairPlay），任何自製接收器都不可能繞過。請直接在 PC 上開 Netflix。
- YouTube 的畫面控制鈕偶爾會卡住（少見；重開即可清除）。
- 極高解析度很吃頻寬；Wi‑Fi 較弱時可能偶爾掉幀。

---

## Credits / 致謝
- **[xenos1337/AirPlayServer](https://github.com/xenos1337/AirPlayServer)** — the base receiver this project is built on. 本專案的直接基礎。
- **[fingergit/airplay2-win](https://github.com/fingergit/airplay2-win)** — the original project xenos built upon. 更上游的原始專案。
- Bundled open‑source libraries: SDL2, FFmpeg, Dear ImGui, Apple Bonjour (mDNSResponder), libplist, FDK‑AAC.

## License / 授權
MIT License — see [LICENSE](LICENSE). Original copyright © 2025 xenos1337 is retained.
MIT 授權 —— 見 [LICENSE](LICENSE)，保留原作者 xenos1337 的版權聲明。
