CareOS v9

CareOS is a high-performance, minimalist 64-bit operating system built entirely from scratch. Designed with both responsiveness and extensibility in mind, it combines a modern graphical environment with low-level system control, offering a clean foundation for experimentation, learning, and future expansion.

<p align="center"> <img src="https://img.shields.io/badge/CareOS-v9.0-blue?style=for-the-badge"> <img src="https://img.shields.io/badge/License-GPLv3-green?style=for-the-badge"> <img src="https://img.shields.io/badge/Platform-x86__64-orange?style=for-the-badge"> </p>
✨ Overview

CareOS is built to explore what a modern, lightweight operating system can look like when performance, simplicity, and visual polish are prioritized together.

It includes a custom kernel, window manager, networking stack, and userland applications, all designed to work cohesively without unnecessary abstraction layers.

🚀 Core Features
🧠 Kernel Architecture
Fully 64-bit kernel targeting x86_64
Preemptive multitasking with process scheduling
Paging + virtual memory management
Modular structure for future scalability
🎨 Graphical Environment
Custom window manager
Glassmorphic UI design
Alpha transparency
Window shadows
Smooth animations
GPU-independent rendering via:
32-bpp linear framebuffer
SSE2 optimizations for performance
🌐 Networking Stack
Native Ethernet support (Intel e1000)
IPv4 networking implementation
Experimental:
TLS 1.3
Basic HTTPS support
💻 Shell & System Tools
Interactive command-line interface
Built-in utilities:
lspci for hardware inspection
File system navigation
File manipulation tools
Designed to integrate tightly with the OS internals
🛠️ Built-in Applications
🌍 Browser
Lightweight rendering engine
Basic HTML/CSS support
Networking integration with OS stack
📊 System Monitor
Real-time tracking of:
CPU usage
Memory usage
Hardware activity
📝 Editor / Notes
Persistent file editing
Clipboard support
Minimal and fast interface
📁 File Manager
Visual file navigation
Directory management
Integrated with system storage layer
🧱 Tech Stack
Component	Technology
Kernel	C, x86_64 Assembly
Bootloader	GRUB (Multiboot2)
Build System	GNU Make, GCC, NASM
Virtualization	QEMU
Graphics	Linear Framebuffer (32-bpp)
🚀 Getting Started
📦 Prerequisites

Make sure you have the following installed:

gcc (x86_64-elf cross-compiler recommended)
nasm
make
qemu-system-x86_64
grub-mkrescue
🔧 Build & Run
make clean
make run

This will:

Compile the kernel and modules
Build a bootable ISO
Launch CareOS in QEMU
⌨️ Keyboard Shortcuts
Shortcut	Action
Alt + Tab	Switch windows
Alt + F4	Close active window
Ctrl + 1–4	Switch virtual desktops
Super (Win)	Open app launcher
🤝 Contributing

CareOS is designed to grow through community contributions while maintaining a consistent vision and architecture.

How it works:
All contributions go through pull requests
Changes are reviewed before being merged into the main OS
The main branch remains the official, curated version
Steps to contribute:
Fork the repository

Create a branch

git checkout -b feature/YourFeature
Commit your changes
Push your branch
Open a Pull Request
Contribution Goals
Improve performance
Expand hardware support
Enhance UI/UX
Add system tools and applications
Strengthen networking capabilities
📄 License

CareOS is licensed under the GNU General Public License v3 (GPLv3).

This ensures:

The project remains free and open
All derivatives must also remain open source
Proper credit is preserved

See the LICENSE file for full details.

🌱 Vision

CareOS aims to evolve from a single-developer system into a collaborative platform driven by experimentation, creativity, and technical curiosity.

The goal isn’t just to build an OS —
it’s to build something people actually want to explore, modify, and improve.