# CareOS v9

CareOS is a high-performance, minimalist 64-bit operating system built from scratch. It features a modern window manager, a robust networking stack, and a custom graphics engine designed for fluid, responsive user experiences.

![CareOS Logo](https://img.shields.io/badge/CareOS-v9.0-blue?style=for-the-badge)
![License](https://img.shields.io/badge/License-GPLv3-green?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-x86__64-orange?style=for-the-badge)

## ✨ Features

- **🚀 64-Bit Kernel**: Fully preemptive multitasking, paging, and memory management.
- **🎨 Glassmorphic GUI**: A modern desktop environment with alpha blending, window shadows, and smooth animations.
- **🌐 Networking Stack**: Built-in Ethernet driver (e1000) with support for IPv4 and experimental TLS 1.3/HTTPS.
- **💻 Interactive Shell**: A feature-rich terminal with support for VFS navigation, hardware inventory (`lspci`), and file manipulation.
- **🛠️ Integrated Apps**: 
  - **Browser**: Modern web rendering with CSS/HTML support.
  - **System Monitor**: Real-time CPU, Memory, and Hardware tracking.
  - **Editor/Notes**: Persistent file editing with clipboard support.
  - **Files**: Visual file management.

## 🛠️ Tech Stack

- **Kernel**: C, x86_64 Assembly
- **Bootloader**: GRUB / Multiboot2
- **Build System**: GNU Make, GCC, NASM
- **Graphics**: 32-bpp Linear Framebuffer with SSE2 optimizations

## 🚀 Getting Started

### Prerequisites

You will need the following tools installed on your system:
- `gcc` (cross-compiler for x86_64-elf recommended, but local gcc works on Linux)
- `nasm`
- `make`
- `qemu-system-x86_64`
- `grub-mkrescue` (for ISO generation)

### Build and Run

To compile the OS and launch it in QEMU:

```bash
make clean
make run
```

### Shortcuts

- **Alt + Tab**: Cycle through windows.
- **Alt + F4**: Close the active window.
- **Ctrl + 1-4**: Switch between virtual desktops.
- **Super (Win)**: Open the App Launcher.

## 🤝 Contributing

We welcome contributions! To ensure CareOS remains free and open for everyone, this project is licensed under the **GPLv3**. 

1. Fork the project.
2. Create your feature branch (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`).
4. Push to the branch (`git push origin feature/AmazingFeature`).
5. Open a Pull Request.

## 📄 License

Distributed under the GNU General Public License v3. See `LICENSE` for more information.

---
*CareOS - A New Standard for Minimalist Computing.*
