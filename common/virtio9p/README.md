# fs-virtio9p — QNX Virtio-9P Filesystem Driver

A user-space QNX resource manager that speaks the 9P2000.L protocol over virtio
transport, enabling QEMU to share host directories directly into the QNX guest.
Used by the QNX unit test infrastructure to share test binaries and runfiles from
the host into the QEMU VM without creating a disk image.

## Architecture

```
QNX app → POSIX call → fs-virtio9p (resmgr) → virtio transport → QEMU → host filesystem
```

### Transport layer

- **PCI (x86_64)**: Auto-discovers the virtio-9p device on the PCI bus
  (vendor `0x1AF4`, device `0x1009`), maps BARs via virtio PCI capability
  parsing, and uses MSI-X interrupts. Falls back to legacy INTx if MSI-X
  setup fails.
- **MMIO (ARM64)**: Uses memory-mapped I/O with explicit base address and IRQ.
  Supports both legacy (v1) and modern (v2) virtio MMIO device layouts.

## Usage

The filesystem is mount-compatible and can be invoked via the `mount_virtio9p`
helper script, which daemonizes `fs-virtio9p` after the mount point is live.

### ARM64 (MMIO)
```sh
mount_virtio9p -o smem=0xa003600,irq=75 none /opt/tests
```

### x86_64 (PCI auto-discovery)
```sh
mount_virtio9p -o transport=pci none /opt/tests
```

### Direct invocation (foreground, for debugging)
```sh
fs-virtio9p -o smem=0xa003600,irq=75,transport=mmio /mnt/host
```

### Mount options

| Option | Description | Default |
|---|---|---|
| `transport=mmio\|pci` | Virtio transport type | `pci` |
| `smem=<addr>` | MMIO base address (implies `transport=mmio`) | — |
| `irq=<n>` | IRQ number for MMIO transport | — |

## Components

```
common/virtio9p/
├── include/
│   ├── protocol/       # 9P2000.L message types, session, fid pool
│   ├── transport/       # Transport interface, MMIO, PCI, virtqueue, virtio defs
│   └── resmgr/          # QNX resmgr config and entry point
├── src/
│   ├── protocol/        # Message serialization, session lifecycle, fid allocation
│   ├── transport/       # MMIO transport, PCI transport, virtqueue impl
│   ├── resmgr/          # QNX resmgr handlers (open, read, stat, close), arg parsing
│   └── main.cpp
├── mount_virtio9p       # Mount helper shell script
└── BUILD
```

- **9P Protocol Library** (`src/protocol/`, `include/protocol/`): 9P2000.L
  message serialization, fid management, and session handling.
- **Virtio Transport** (`src/transport/`, `include/transport/`): Split virtqueue
  management over MMIO or PCI. QNX-only (uses `mmap_device_memory`,
  `InterruptAttachEvent`, PCI config space APIs).
- **Resource Manager** (`src/resmgr/`): QNX resmgr integration mapping POSIX
  `open`/`read`/`write`/`stat`/`close`/`mkdir`/`unlink`/`rename` to 9P
  `Walk`/`Open`/`Read`/`Write`/`Create`/`GetAttr`/`Mkdir`/`Unlink`/`Rename`/`Clunk`.
  Custom OCB allocator embeds per-file 9P state directly.
- **Mount Helper** (`mount_virtio9p`): Shell script translating mount-style
  arguments and daemonizing the resource manager.

## Protocol

9P2000.L (Linux extension of 9P2000), natively supported by QEMU's virtio-9p
backend. Security model `none` (test infrastructure only).

## PCI interrupt handling

With `DO_BUS_CONFIG=no` in the QNX pci-server config, SeaBIOS has already
performed PCI enumeration and IRQ assignment before QNX boots. The PCI transport
programs MSI-X directly (allocating an IRQ from the kernel's MSI range via
`rsrcdbmgr`, programming the MSI-X table entry in BAR MMIO, and enabling MSI-X
in PCI Message Control). This avoids any dependency on `pci_hw.cfg` INTx
interrupt mappings.

## Current scope

Read-write filesystem access for test data sharing. Read path: test binaries,
runfiles, filter files. Write path: test results, coverage archives, and
arbitrary files written back to the host shared directory.

Supported write operations:
- File creation (`open` with `O_CREAT`)
- File writes (`write`, `pwrite`)
- Directory creation (`mkdir`)
- File and directory removal (`unlink`, `rmdir`)
- Rename (`rename`)

Not supported (not needed for test flows): `chmod`/`chown`/`utime` (`Tsetattr`),
symlinks (`Tsymlink`), device nodes (`Tmknod`), file locking (`Tlock`).

> **Note:** The QEMU `-virtfs` share must not be mounted `readonly` for write
> operations to succeed.

## Build

The Bazel targets are defined in `BUILD`:
- `nine_p_protocol` — 9P protocol library (QNX-only)
- `virtio_transport` — QNX-only transport library
- `fs-virtio9p` — QNX binary
