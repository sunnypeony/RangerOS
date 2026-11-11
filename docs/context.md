# 栈与上下文布局

本文件按 SPEC D01/D03/S02 记录栈布局。M0 只有启动栈；M1 增加异常现场（trap frame）。
任务 context 与 switch_context 属于 M4a，本文件尚不涉及。

文中地址取自提交 `ae9e04d` 的构建产物（build/kernel.map、实际运行日志），代码变化后地址会变化，偏移不会。

## 1. 启动栈（M0 起）

- 位置：`.bss` 中的 `boot_stack_bottom`..`boot_stack_top`，16 KiB，16 字节对齐（arch/i386/boot.asm）。
  当前链接结果：`0x103000`..`0x107000`。`.bss` 位于独立的 RW `PT_LOAD`（memsz 0x483c），GRUB 会为其保留内存；
  check_elf.py 的 `alloc-sections-loaded` 检查保证这一点。
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

- `kernel_main` 不返回（结尾 `cpu_halt_forever()`）；即使返回，也会落到 `_start.hang`（`cli; hlt; jmp`）。
- 没有栈哨兵或 guard page（无分页）；栈溢出会静默覆盖 `.bss` 中位于栈下方的数据。

## 2. 正常 C 调用栈与异常现场的区别

| | 正常 C 调用（`call`/`ret`） | 异常进入（CPU 经 IDT 进入，`iret` 返回） |
|---|---|---|
| 谁压栈 | 调用者压参数，`call` 压返回 EIP | CPU 压 EFLAGS、CS、EIP，部分向量再压 error code |
| 是否可预期 | 由编译器按 ABI 安排，位置在编译时已知 | 可能发生在任意指令上，被打断的代码不知道 |
| 返回 | `ret` 只弹出 EIP | `iret` 弹出 EIP、CS、EFLAGS（会恢复 IF） |
| 需要保存的寄存器 | 被调用者只保存 callee-saved（EBX、ESI、EDI、EBP） | 入口必须保存全部通用寄存器和段寄存器，因为被打断的代码没有为此做任何准备 |
| 栈对齐 | ABI 保证 `call` 时 16 字节对齐 | CPU 不保证；isr_common 先手动对齐，再调用 C |
| IF | 不变 | 中断门（type 0xE）进入时 CPU 清 IF |

本内核只运行在 ring 0，所以异常发生时**不切换栈**：CPU 直接在被打断代码的当前栈上压现场，不压 SS:ESP。
因此异常现场与正常调用帧在同一个栈上，紧贴在被打断代码的栈顶之下。

## 3. 异常现场（trap frame，M1）

### 3.1 压栈顺序

1. CPU：`EFLAGS`、`CS`、`EIP`（EIP 指向出错指令本身，适用于 #UD、#GP 这类 fault）。
2. CPU：只有向量 8、10–14、17、21、29、30 会压 error code。
3. 存根（isr.asm 中每个向量一个）：其余向量压一个**人工补的 0**，使所有向量的布局一致；然后压向量号。
4. `isr_common`：`pushad`，再依次压 DS、ES、FS、GS（先清零 EAX，再经 EAX 零扩展为 32 位后压栈）。

### 3.2 偏移表（低地址在上；与 arch/i386/trapframe.h 的 `struct trap_frame` 一致，由 `_Static_assert` 固定）

```
偏移   字段        压入者            说明
+0x00  gs          isr_common
+0x04  fs          isr_common
+0x08  es          isr_common
+0x0C  ds          isr_common
+0x10  edi         pushad
+0x14  esi         pushad
+0x18  ebp         pushad
+0x1C  esp_pusha   pushad            pushad 之前的 ESP = frame + 0x30（指向 vector），不是被打断代码的 ESP
+0x20  ebx         pushad
+0x24  edx         pushad
+0x28  ecx         pushad
+0x2C  eax         pushad
+0x30  vector      存根
+0x34  error       CPU 或存根（补 0）
+0x38  eip         CPU
+0x3C  cs          CPU
+0x40  eflags      CPU
+0x44  ---         被打断代码异常前的 ESP（ring 0 不切栈，所以就是 frame + 0x44）
```

`trap_frame_interrupted_esp(tf) = tf + 68`。诊断里打印的 `esp=` 就是这个值；`pusha_esp=` 是 pushad 保存的值，两者不同。

### 3.3 实例：#GP 自检（docs/evidence/M1-logs/m1-gp.serial.log）

```
地址          偏移    内容                      来源
0x00106fdc    +0x44   （异常前的栈顶）          被打断代码的 ESP（日志 esp=0x00106fdc）
0x00106fd8    +0x40   eflags = 0x00000002       CPU（IF=0）
0x00106fd4    +0x3C   cs     = 0x00000008       CPU
0x00106fd0    +0x38   eip    = 0x00101c46       CPU：mov %eax,%ds（selftest_fault_site）
0x00106fcc    +0x34   error  = 0x00000028       CPU：#GP 错误码 = 选择子 0x28
0x00106fc8    +0x30   vector = 13               存根          <- 日志 pusha_esp=0x00106fc8
0x00106fc4    +0x2C   eax    = 0x00000028       pushad
0x00106fc0    +0x28   ecx    = 0x00000140       pushad
0x00106fbc    +0x24   edx    = 0x000003d5       pushad
0x00106fb8    +0x20   ebx    = 0x00010000       pushad
0x00106fb4    +0x1C   esp_pusha = 0x00106fc8    pushad
0x00106fb0    +0x18   ebp    = 0x00106fdc       pushad
0x00106fac    +0x14   esi    = 0x2badb002       pushad
0x00106fa8    +0x10   edi    = 0x0010783c       pushad
0x00106fa4    +0x0C   ds     = 0x00000010       isr_common
0x00106fa0    +0x08   es     = 0x00000010       isr_common
0x00106f9c    +0x04   fs     = 0x00000010       isr_common
0x00106f98    +0x00   gs     = 0x00000010       isr_common    <- frame（struct trap_frame *）
```

check_exception.py 用日志验证 `pusha_esp == frame + 0x30`、`esp == frame + 0x44`。
DS 仍是 0x10：出错的 `mov %eax,%ds` 没有完成，CPU 报告 fault 时不改变目标寄存器。

### 3.4 进入 C 与返回

```
mov ebx, esp        ; EBX = frame（callee-saved，C 调用后不变）
and esp, -16        ; 对齐
sub esp, 12
push ebx            ; 参数；call 时 ESP 16 字节对齐
call isr_dispatch
mov esp, ebx        ; 回到 frame
pop gs/fs/es/ds（经 EAX），popad，add esp, 8（丢弃 vector 与 error），iretd
```

M1 中 `isr_dispatch` 对所有向量都调用 `panic()`，不会返回，所以**返回路径存在但未被执行或测试**。
M2 处理 IRQ 时会第一次走这条路径。

### 3.5 中断状态

- IDT 256 项都是 DPL=0 的 32 位中断门（0x8E），进入时 CPU 清 IF。
- M1 全程不执行 `sti`，PIC 未重映射（BIOS 默认 IRQ0–7 落在 0x08–0x0F，与异常重叠），所以在 M2 完成 remap 之前绝不能开中断。
- `panic()` 先 `cli`，再输出 `PANIC …` 与 `HALT`，然后 `cli; hlt` 循环；不返回，也不调度。
- 嵌套保护：报告异常时再次出错，只用串口输出一行后停机；panic 中再次 panic 同理。这两条路径尚未测试。
