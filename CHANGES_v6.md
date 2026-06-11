# CareOS v6 - Change Log

## Session and Login

- Fixed post-login freeze by keeping timer ticks advancing after scheduler IRQ0 hook.
- Removed automatic boot-time login.
- Added a dedicated GUI login screen with username/password fields.
- Added failure tracking and temporary lockout after repeated bad attempts.
- Kept existing account model (`root`, `user`) and integrated it into startup flow.

## Boot and First-Run UX

- Upgraded splash flow to a staged loader with clearer state feedback.
- Added visual polish to startup while keeping boot sequence simple and fast.

## Window Manager and Desktop

- Fixed launcher overlay path so it is both drawn and interactive.
- Added event consumption for launcher interactions so clicks do not leak to underlying windows.

## Filesystem Reliability

- `vfs_mkdir` now returns existing directories and refuses dir/file type collisions.
- `vfs_mkfile` now returns existing files and refuses file/dir type collisions.
- `vfs_delete` now recursively wipes subtree contents to avoid stale orphan nodes.

## Safety Fixes

- Reworked `vfs_get_path` string construction to use bounded writes and avoid overflow-prone concatenation.

## Persistence

- User accounts now persist on the ATA disk image (`careos.img`).
- Created users and password changes survive reboot.
- `make clean` now preserves `careos.img`.
- `make reset-disk` recreates a blank disk and wipes persistent users/data.

## Notes

This release intentionally builds on top of the existing architecture and apps, focusing on reliability, polish, and a more real OS-style startup experience without overcomplicating the codebase.
