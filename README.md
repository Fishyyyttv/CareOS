# 🖥️ CareOS

A custom 64-bit operating system built from scratch — featuring a graphical environment, custom apps, and even DOOM running natively inside it.

---

## ✨ About

CareOS is a low-level operating system project focused on understanding how systems actually work — from the kernel to the UI.

It combines core OS concepts (memory, multitasking, rendering) with higher-level features like a window manager and applications.

This is not just a kernel demo — it's an evolving, full system.

---

## 🚀 Features

### 🧠 Kernel
- 64-bit x86_64 architecture
- Written in C + Assembly
- Paging-based memory management
- Basic multitasking
- Custom libc implementation (headers + shim layer)

---

### 🎨 Graphics / Window Manager
- Custom window manager
- 32-bit linear framebuffer rendering
- Window focus + z-order system
- Taskbar
- Basic UI rendering + drawing routines

---

### 🎮 Applications

| App        | Description |
|------------|------------|
| 🎮 DOOM    | DOOM port using doomgeneric, runs inside CareOS |
| 🧱 3D Demo | Basic 3D rendering experiment |
| 🧩 Maze    | Simple interactive demo application |

---

### ⚙️ System
- App launching system
- Internal app framework
- Modular structure for adding new programs

---

## 🧱 Tech Stack

| Layer        | Technology |
|-------------|-----------|
| Kernel       | C, x86_64 Assembly |
| Bootloader   | GRUB (Multiboot2) |
| Build System | GNU Make, GCC, NASM |
| Runtime      | QEMU |
| Graphics     | 32-bpp Linear Framebuffer |

---

## 🚀 Getting Started

### 📦 Requirements

- `gcc` (cross-compiler recommended)
- `nasm`
- `make`
- `qemu-system-x86_64`
- `grub-mkrescue`

---

### 🔧 Build & Run

```bash
make clean
make run
