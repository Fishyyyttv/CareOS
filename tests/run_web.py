"""Build a separate test ISO and exercise Weave in QEMU, without a user disk."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
build = root / 'build/web-smoke'
iso = build / 'iso'
(iso / 'boot/grub').mkdir(parents=True, exist_ok=True)
subprocess.run(['make', '-j8', 'kernel/kernel.elf'], cwd=root, check=True)
subprocess.run(['objcopy', '--strip-debug', str(root / 'kernel/kernel.elf'), str(iso / 'boot/kernel.elf')], check=True)
(iso / 'boot/grub/grub.cfg').write_text('''set timeout=0
set default=0
menuentry "Weave JavaScript tests" {
    multiboot2 /boot/kernel.elf web-selftest
    boot
}
''')
with (build / 'iso.log').open('w') as log:
    subprocess.run(['grub-mkrescue', '-o', str(build / 'test.iso'), str(iso)], check=True, stdout=log, stderr=subprocess.STDOUT)
log_path = build / 'qemu.log'
with log_path.open('w') as log:
    result = subprocess.run(['qemu-system-x86_64', '-m', '1024M', '-smp', '1',
        '-accel', 'tcg', '-cpu', 'max', '-cdrom', str(build / 'test.iso'),
        '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot',
        '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04'],
        stdout=log, stderr=subprocess.STDOUT, timeout=60)
text = log_path.read_text()
print(text[-10000:])
if result.returncode != 1 or '[web-test] ALL PASSED' not in text:
    raise SystemExit(f'Weave kernel tests failed (QEMU exit {result.returncode}); see {log_path}')
print('Weave kernel integration suite passed.')
