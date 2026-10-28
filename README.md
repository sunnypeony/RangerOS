# RangerOS

32 位 x86 教学内核（ring 0 内核线程，GRUB Multiboot 1）。当前阶段：**M0 — 构建与启动**。
设计约束见 [SPEC.md](SPEC.md)，里程碑见 [MILESTONES.md](MILESTONES.md)，M0 证据见 [docs/evidence/M0.md](docs/evidence/M0.md)。

## 环境

所有命令都在 Ubuntu 20.04 ARM64 虚拟机（Mac 上的 UTM）中的 `~/projects/RangerOS` 执行。
宿主不是 x86，所以用 clang 交叉编译到 i386，不用 `gcc -m32`（见 SPEC.md §2.1）。

安装依赖（一次性）：

```bash
sudo apt install build-essential nasm clang lld llvm python3 qemu-system-x86 xorriso mtools grub-common
```

宿主 arm64 apt 不提供 GRUB 的 i386-pc 模块，所以改从 Ubuntu 软件包中解出来，放在 `tools/grub/`（本地已有；.deb 与解包目录不纳入 git，按下列命令重建）：

```bash
cd tools/grub
wget https://archive.ubuntu.com/ubuntu/pool/main/g/grub2/grub-pc-bin_2.04-1ubuntu26.17_amd64.deb
sha256sum grub-pc-bin_2.04-1ubuntu26.17_amd64.deb   # eb71891e5f7eec9f042232ae23910e38392a6f1302170f07575bac07620b70b5
dpkg-deb -x grub-pc-bin_2.04-1ubuntu26.17_amd64.deb extracted
```

`make versions` 把工具版本写入 `build/versions.txt`。

## 构建

```bash
make clean && make
```

产物：

| 文件 | 内容 |
|---|---|
| `build/kernel.elf` | ELF32 i386 内核，带 DWARF 调试符号 |
| `build/kernel.map` | ld.lld 链接 map |
| `build/rangeros.iso` | GRUB（i386-pc，BIOS）可启动 ISO |

`make check`：用 readelf/nm/grub-file 检查 ELF 与 Multiboot 头。

## 运行

```bash
make run
```

QEMU 以 `-nographic` 运行（TCG、单 CPU、128 MiB、`-no-reboot`），GRUB 与内核输出都在 COM1 串口，也就是当前终端。
看到 `BOOT_OK` 后内核以 `cli; hlt` 停机。

**退出 QEMU：按 `Ctrl-A`，松开，再按 `X`。**（`Ctrl-A H` 列出其他串口控制键。）

## 测试

```bash
make test
```

先构建内核、ISO 和负向自检镜像，再运行 `tests/scripts/m0_test.sh`；所有用例都得到预期结果才退出 0。
日志写到 `build/test-logs/`，摘要在 `build/test-logs/summary.txt`。用例说明见 docs/evidence/M0.md。

## 目录

```
arch/i386/   boot.asm（Multiboot 头、入口、初始栈）、linker.ld、io.h
drivers/     serial.c（COM1）
kernel/      kmain.c
include/     rangeros/*.h
boot/grub/   grub.cfg
tests/       scripts/（ELF 检查、QEMU 运行器、M0 套件）、grub/（负向自检配置）
docs/        context.md（栈布局）、evidence/
env-check/   M0 之前的人工环境检查（保留）
tools/       GRUB i386-pc 模块（保留）
```
