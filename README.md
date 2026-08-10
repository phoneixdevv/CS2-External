# 🎮 CS2-External

External tool for https://siberdeyiz.com Counter-Strike 2 written in C++.

The project started as a small external project and gradually grew into a collection of different features. The main goal is to keep things simple and lightweight while still having enough options to customize how everything behaves.

> ⚠️ **Note:** This project is provided for educational and research purposes. Use it at your own risk.

## ✨ Features

### 👁️ ESP

* 📦 Box ESP

  * Full box
  * Corner box
  * 3D box
* 👤 Player names
* ❤️ Health bar
* 🛡️ Armor
* 🦴 Skeleton
* 🔫 Weapon info
* 📏 Distance
* ➖ Snaplines
* 🎯 Head dot
* 💣 Bomb ESP
* 👥 Team ESP
* 🎨 Custom ESP colors

### 🎯 Aimbot

* FOV based targeting
* Adjustable smoothness
* Configurable target bone
* FOV circle
* Visibility check
* Different target selection modes
* Custom activation key
* Optional automatic shooting

### 🔫 Triggerbot

* Configurable activation key
* Adjustable delay
* Automatic stop option

### 🛠️ Other

* 📡 Radar
* ✨ Glow
* 🐰 Bunny hop
* 🚫 No flash
* 👀 FOV changer
* ➕ Custom crosshair
* 👻 Spectator list
* 🏆 Rank information
* 💰 Economy information

Some features are still being worked on and may change over time.

## 🖥️ Requirements

* 🪟 Windows 10 / 11
* 💻 Visual Studio 2022
* ⚙️ Desktop development with C++
* 🔧 MSVC v143
* 📦 Windows SDK
* x64

The project currently uses C++20 for the Release configuration.

## 🔨 Building

Clone the repository:

```bash
git clone https://github.com/phoneixdevv/CS2-External.git
```

Open `cs2ext.sln` with Visual Studio.

Select:

```text
Configuration: Release
Platform: x64
```

Then build the solution.

If you are getting build errors, make sure the required Visual Studio C++ components and Windows SDK are installed.

## 📁 Project Structure

```text
CS2-External/
│
├── cs2ext/
│   ├── cheats/
│   │   ├── features/
│   │   │   ├── aimbot.cpp
│   │   │   ├── aimbot.h
│   │   │   ├── glow.cpp
│   │   │   ├── glow.h
│   │   │   ├── misc.cpp
│   │   │   ├── misc.h
│   │   │   ├── triggerbot.cpp
│   │   │   └── triggerbot.h
│   │   │
│   │   ├── Esp.cpp
│   │   ├── Esp.h
│   │   ├── config.h
│   │   └── render.h
│   │
│   ├── imgui/
│   ├── math/
│   ├── mem/
│   ├── offsets/
│   ├── tools/
│   ├── cs2ext.cpp
│   └── cs2ext.vcxproj
│
├── cs2ext.sln
├── LICENSE
└── README.md
```

## ⚙️ Configuration

Most settings can be found in:

```text
cs2ext/cheats/config.h
```

This includes settings for things such as ESP, aimbot, triggerbot, colors, FOV, crosshair and miscellaneous features.

The configuration is saved locally so your settings don't have to be set again every time.

## 🔄 Offsets

CS2 updates can change offsets and break parts of the project.

Offsets are located in:

```text
cs2ext/offsets/
```

There are also tools in the `tools` directory for handling offset updates.

If the game has received an update and something suddenly stops working, outdated offsets are one of the first things to check.

## 📦 Dependencies

The project uses:

* 🖼️ Dear ImGui
* 🎨 DirectX 11
* 🪟 Win32 API
* 🔄 DXGI
* 🖌️ Direct2D / DirectWrite

Dear ImGui is used for the menu and rendering interface.

## 🤝 Contributing

Pull requests are welcome.

If you want to add something, try to keep it consistent with the existing code instead of adding everything directly to the main source files.

For larger changes, opening an issue first is recommended.

## 📜 License

This project is licensed under the **Apache License 2.0**.

See [`LICENSE`](LICENSE) for the full license text.

## ⚠️ Disclaimer

This project is intended for educational and research purposes.

I am not responsible for any bans, account restrictions, data loss or other problems caused by using this project.

**Use it responsibly.**

## ❤️ Credits

* [Dear ImGui](https://github.com/ocornut/imgui)
* Valve / Counter-Strike 2
* Everyone who has contributed to the project

## 👤 Author

**phoneixdevv**

⭐ If you find the project useful, consider giving the repository a star.

🔗 [https://github.com/phoneixdevv/CS2-External](https://github.com/phoneixdevv/CS2-External)
