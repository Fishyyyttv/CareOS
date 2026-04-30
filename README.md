<h1 align="center">🖥️ CareOS v9</h1>

<p align="center">
  <b>A high-performance, minimalist 64-bit operating system built from scratch.</b><br>
  Designed for speed, control, and a modern user experience.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-v9.0-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/license-GPLv3-green?style=for-the-badge">
  <img src="https://img.shields.io/badge/platform-x86__64-orange?style=for-the-badge">
</p>

---

## ✨ About

**CareOS** is a fully custom-built operating system focused on performance, simplicity, and visual polish.  
It combines a low-level kernel with a modern graphical environment and integrated applications.

Built for:
- 🧠 Learning how operating systems actually work  
- ⚡ High performance with minimal overhead  
- 🎨 Clean, modern UI experimentation  

---

## 🚀 Features

### 🧠 Kernel
- 64-bit x86_64 architecture  
- Preemptive multitasking  
- Virtual memory + paging  
- Modular and extensible design  

---

### 🎨 GUI / Window Manager
- Custom window manager  
- Glassmorphic UI (blur, transparency, shadows)  
- Smooth animations  
- 32-bpp framebuffer rendering  
- SSE2-optimized graphics routines  

---

### 🌐 Networking
- Intel **e1000** Ethernet driver  
- IPv4 networking stack  
- Experimental support:
  - TLS 1.3  
  - HTTPS requests  

---

### 💻 Shell & System Tools
- Interactive command shell  
- File system navigation  
- Hardware inspection (`lspci`)  
- Core system utilities  

---

### 🛠️ Built-in Applications

| App | Description |
|-----|------------|
| 🌍 Browser | Lightweight HTML/CSS rendering engine |
| 📊 System Monitor | Real-time CPU, memory, and hardware stats |
| 📝 Editor | Persistent notes and file editing |
| 📁 Files | Visual file manager |

---

## 🧱 Tech Stack

| Layer | Technology |
|------|----------|
| Kernel | C, x86_64 Assembly |
| Bootloader | GRUB (Multiboot2) |
| Build System | GNU Make, GCC, NASM |
| Virtualization | QEMU |
| Graphics | 32-bpp Linear Framebuffer |

---

## 🚀 Getting Started

### 📦 Requirements

Make sure you have:

- `gcc` (x86_64-elf cross-compiler recommended)  
- `nasm`  
- `make`  
- `qemu-system-x86_64`  
- `grub-mkrescue`  

---

### 🔧 Build & Run

```bash
make clean
make run

This will:

Build the kernel
Generate a bootable ISO
Launch CareOS in QEMU
⌨️ Shortcuts
Key	Action
Alt + Tab	Switch windows
Alt + F4	Close window
Ctrl + 1–4	Virtual desktops
Super	App launcher
📸 Screenshots

Coming soon — because every OS looks fake until proven otherwise.

🤝 Contributing

CareOS is open to contributions while keeping a strong core direction.

Workflow
Fork the repo

Create a branch

git checkout -b feature/YourFeature
Commit changes
Push
Open a Pull Request
Guidelines
Keep code clean and consistent
Focus on performance + simplicity
Avoid unnecessary abstraction
Discuss large changes before implementing

All changes are reviewed before merging into the main OS.

📄 License

Licensed under GNU GPL v3.

✔ You can use and modify the code
✔ Must remain open source
✔ Must include original credit

See LICENSE for details.

🌱 Vision

CareOS is evolving from a solo-built system into a collaborative platform.

The goal:

Build an OS people don’t just use — but understand, modify, and improve. ```
