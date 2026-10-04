# Phase 0 — Hardware Platform Design

**Date:** 2026-10-03 · Part of the v10 roadmap (`2026-10-03-careos-v10-redesign-roadmap.md`)

## Goal

CareOS uses what the VM gives it:
- every CPU core
- all of the RAM
- whatever disk is attached, at its full size, on IDE or SATA
- the VirtualBox VMSVGA adapter with its VRAM

It also reports all of this truthfully.

## Current state (verified 2026-10-03)

| Area | Today |
|---|---|
| CPU | One core. No ACPI/MADT parsing, no AP startup. `/proc/cpuinfo` is hard-coded "CareOS Virtual CPU" (`kernel/vfs.c:704`) |
| RAM | Multiboot map read (`kernel/mb2mmap.c`), but physical memory is identity-mapped only for 0–4 GB with 2 MB pages (`kernel/paging.c:233`) and the frame bitmap is fixed to 4 GB. A 20 GB VM uses about 4 GB |
| Storage | `drivers/storage/ata.c`: PIO, LBA28 reads/writes; LBA48 support detected but unused. No AHCI, so a SATA disk is invisible |
| Display | `drivers/vesa.c`: Bochs BGA ports only. Software rendering to an uncached → write-combining framebuffer |
| VM `careos` | 4 CPUs, 20 GB RAM, `vboxvga`, **9 MB VRAM**, IDE disk 12 GB |

## Design

### 1. Discovery — `kernel/acpi.c`, `kernel/hwinfo.c`
- Find the RSDP: the multiboot2 ACPI tags (new/old RSDP) first, then a scan of the EBDA and 0xE0000–0xFFFFF. Checksum-validate it.
- Walk the XSDT if present, otherwise the RSDT. Parse:
  - **MADT**: LAPIC base, per-CPU LAPIC IDs + enabled flag, I/O APIC id/address/GSI base, interrupt source overrides (ISA IRQ → GSI, polarity, trigger)
  - **HPET**: base address, optional
  - **FADT**: reset register and PM1 control for power-off. Replaces today's ad-hoc shutdown if valid.
- CPUID: vendor, brand string (leaves 0x80000002–4), family/model/stepping, feature flags (APIC, x2APIC, 1 GB pages, invariant TSC), logical cores, cache sizes (leaf 4 / 0x80000006).
- `hwinfo_t` (in `kernel/hwinfo.h`, frozen at W0) collects:
  - CPU: brand, vendor, cores_present, cores_online, features, TSC MHz (calibrated against the PIT)
  - RAM: total usable bytes, highest physical address
  - disks: up to 8 entries {controller, model, serial, sectors, sector_size}
  - display: {adapter name, PCI id, VRAM bytes, current mode, max mode}
  - NICs: {model, MAC, link}
- Consumers:
  - `/proc/cpuinfo` (one block per online core) and `/proc/meminfo`
  - a new `hwinfo` terminal command
  - Settings › About
  - menu-bar "About this computer"

### 2. All cores — `kernel/smp.c`, `kernel/ap_trampoline.asm`, `kernel/workpool.c`
- **Interrupts:** mask the legacy 8259 PIC, enable the BSP LAPIC, and program the I/O APIC redirection table from the MADT overrides (timer, keyboard, mouse, ATA, AHCI, e1000). The PIT keeps running the existing 1 kHz timebase through the I/O APIC, so `timer_ms()` doesn't change. If there's no MADT, stay on the PIC with one core.
- **AP startup:**
  - Copy the trampoline to a free page below 1 MB, found from the memory map.
  - Send INIT → 10 ms → SIPI → 200 µs → SIPI to each enabled LAPIC.
  - Each AP goes real → protected → long mode on the kernel PML4 and loads its own GDT + TSS, the shared IDT, a 16 KB stack and per-CPU data via GS base.
  - Each AP marks itself online, then enters `workpool_ap_loop()`.
  - Timeout: 100 ms per AP, then it's reported as offline. Boot never hangs.
- **Worker pool** (`kernel/workpool.h`, frozen at W0):
  - `wp_parallel_for(u32 n, void (*fn)(u32 i, void *ctx), void *ctx)`: splits `n` items across the online cores, the BSP included. Returns when every item is done.
  - `wp_submit(fn, arg)` / `wp_wait(handle)` for single async jobs.
  - `wp_cores()`.
  - Jobs **must not** allocate, take kernel locks, touch the VFS, network or scheduler, or block. They work only on buffers passed in.
  - Queue: a fixed-size ring per core with an atomic head/tail. APs `hlt` when idle and are woken by IPI vector 0xF0.
  - A single-core build or `smp=off` runs `fn` inline on the BSP.
- APs never run scheduler tasks in phase 0. The scheduler, heap, VFS and network stay BSP-only, so no kernel locking changes are needed.
- First users (phase 1 wires them up): blur bands, wallpaper scaling, QOI/PNG decode, alpha blits of large regions.

### 3. All RAM — `kernel/paging.c`, PMM
- Highest usable physical address = max over the multiboot usable ranges.
- Identity-map 0..max_phys: 1 GB pages if CPUID reports them, otherwise 2 MB pages. Page-directory pages come from early boot memory.
- The frame bitmap is sized `max_phys / 4096 / 8` bytes and placed in the first usable range big enough for it, above the kernel. Only usable ranges are marked free; reserved and ACPI ranges stay used.
- Kernel heap growth limit = min(¼ of RAM, 1 GB). Other limits that were tuned for 2 GB scale with RAM.
- MMIO above max_phys (LAPIC, I/O APIC, HPET, SVGA FIFO, AHCI ABAR) is mapped on demand: uncached, or write-combining for framebuffers.

### 4. Storage — `drivers/storage/`
- **`blockdev.h`** (frozen at W0): `blockdev_t {name, sectors, sector_size, read(lba64,count,buf), write(...)}`, a registry, and `blockdev_primary()`. `ext2` and the VFS switch from direct `ata_*` calls to the primary blockdev.
- **IDE:** LBA48 commands (READ/WRITE SECTORS EXT) when the disk reports it, and bus-master DMA through the PIIX BMIDE BAR4 with a PRD table in identity-mapped memory below 4 GB. Falls back to PIO on error. Probes all four IDE positions; ATAPI devices are recorded but not used as disks.
- **AHCI** (`ahci.c`):
  - Find class 01:06:01 on PCI, map ABAR, enable AHCI mode and bus mastering.
  - For each implemented port with a device: stop, set CLB/FB, start, IDENTIFY.
  - READ/WRITE DMA EXT with one command slot and polling completion. IRQ-driven completion is optional, behind a flag.
  - Maximum transfer 128 sectors per command.
- **Primary disk selection:** the first non-ATAPI disk that holds an ext2 superblock. Otherwise the first disk of at least 64 MB, which the existing first-boot formatter formats to its full capacity.
- An existing ext2 image keeps its size. Growing an existing filesystem is out of scope.

### 5. Display — `drivers/svga.c`
- PCI `15AD:0405`. The I/O BAR0 gives the index/value ports, BAR1 the framebuffer, BAR2 the FIFO.
- Negotiate the SVGA ID (2 → 1 → 0). Read `VRAM_SIZE`, `FB_SIZE`, `MAX_WIDTH`/`MAX_HEIGHT` and `CAPABILITIES`.
- Mode set: `WIDTH`, `HEIGHT`, `BITS_PER_PIXEL=32`, `ENABLE=1`, then read back `BYTES_PER_LINE` and `FB_OFFSET`.
- **FIFO:** init MIN/MAX/NEXT_CMD/STOP and set `CONFIG_DONE`. Present path: `gfx_flip` copies the dirty rects into the framebuffer, then queues one `SVGA_CMD_UPDATE` per dirty rect, coalesced to at most 16 per frame. `SYNC` only when the FIFO is nearly full.
- **Hardware cursor:** `SVGA_CMD_DEFINE_ALPHA_CURSOR` with the existing cursor ARGB, and position through the `CURSOR_*` registers if `SVGA_CAP_CURSOR_BYPASS_3` is set. The software cursor path stays as the fallback.
- Probe order: SVGA, then BGA/VBoxVGA (today), then the multiboot framebuffer. The mode list is all standard modes up to max that fit in VRAM ×2 (double buffer), and the user's saved resolution is respected.
- QEMU `-vga vmware` exercises the same driver.

### 6. Settings › About, `hwinfo`
Settings › About shows, all from `hwinfo_t`:
- CPU brand, "4 of 4 cores online"
- RAM total and in use (bar)
- each disk (controller · model · size)
- display adapter · VRAM · mode
- NIC model · MAC · link

Styling arrives in phase 3; phase 0 delivers the data and a plain list.

## Error handling
- Every probe fails soft: log to serial, mark the device absent, fall back to the next path. Boot never depends on ACPI, SMP, AHCI or SVGA.
- AP startup timeout → that core is reported offline.
- DMA error → retry once in PIO, then return an error to the caller.
- SVGA FIFO stall (`NEXT_CMD` not advancing for 50 ms) → disable SVGA presenting and fall back to plain framebuffer writes with a full `UPDATE` each frame.

## Testing
- **Host unit tests:**
  - RSDP/XSDT/MADT parsers on captured tables from QEMU and VirtualBox, plus malformed ones
  - multiboot map → bitmap sizing and placement
  - AHCI FIS/PRD builders
  - SVGA FIFO ring arithmetic
  - `wp_parallel_for` split math
- **QEMU:** `-smp 4 -m 6G -vga vmware`, an AHCI disk (`-device ahci,id=a -drive if=none,id=d,file=…,format=raw -device ide-hd,drive=d,bus=a.0`) and the IDE disk variant. Serial must show `smp: 4/4 cores online`, `mem: 6.0 GB usable`, `ahci: port0 … GB`, `svga: id2 vram … MB`.
- **Stress:** a worker-pool torture test (1M tiny jobs, checksum verified) at boot under the `qa` build flag.
- **VirtualBox:** the reconfigured VM (4 CPUs, 20 GB, VMSVGA 128 MB, SATA). `hwinfo` output and a Settings › About screenshot. `perf bench` before and after.

## Out of scope
SVGA3D/GPU acceleration, x2APIC mode, NUMA, NVMe, USB storage, ACPI sleep states, running scheduler tasks on APs (phase 6).
