# 栈与上下文布局

本文件按 SPEC D01/S02 记录栈布局。M0 只有启动栈；context/trapframe 部分在 M1（ISR）与 M4a（switch_context）补充。

## 启动栈（M0）

- 位置：`.bss` 中的 `boot_stack_bottom`..`boot_stack_top`，16 KiB，16 字节对齐（arch/i386/boot.asm）。
  当前链接结果：`0x102000`..`0x106000`（以 build/kernel.map 为准）。`.bss` 位于独立的 RW `PT_LOAD`，GRUB 会为其保留内存。
- 进入 `_start` 时（Multiboot 1 约定）：保护模式、分页关闭、IF=0、EAX=0x2BADB002、EBX=multiboot_info 物理地址，ESP 未定义。
- `_start` 依次执行：`cli` → `cld` → 清零 `.bss`（包括栈；魔数暂存在 EDX）→ `ESP = boot_stack_top` → `EBP = 0`。
- i386 SysV ABI 要求 `call` 指令执行时 ESP 16 字节对齐，所以先填充 8 字节，再压两个参数：

```
地址（相对 boot_stack_top = T）  内容
T - 0x04                         填充
T - 0x08                         填充
T - 0x0C                         arg1  multiboot_info（EBX）
T - 0x10                         arg0  magic（原 EAX）   <- call 前 ESP = T - 0x10（16 对齐）
T - 0x14                         返回地址 -> _start.hang  <- 进入 kernel_main 时 ESP
```

- `kernel_main` 正常不返回；若返回，落到 `_start.hang`（`cli; hlt; jmp`），不会跳到未知地址。
- 未实现栈哨兵或 guard page（无分页）；栈溢出会静默覆盖 `.bss` 之前的数据。
