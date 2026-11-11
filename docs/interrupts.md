# 中断、定时器、键盘与输入队列（M2）

本文记录 M2 的中断与输入设计。栈与 trap frame 的偏移见 [context.md](context.md)；证据见 [evidence/M2.md](evidence/M2.md)。

## 1. 向量与 PIC

| 向量 | 来源 | 处理 |
|---|---|---|
| 0x00–0x1F | CPU 异常 | `fatal_trap()`：打印诊断后 panic，不返回（与 M1 相同） |
| 0x20–0x27 | 主 PIC IRQ0–7 | `irq_dispatch(irq)`，**返回**，经 `iretd` 恢复现场 |
| 0x28–0x2F | 从 PIC IRQ8–15 | 同上（当前全部屏蔽） |
| 0x30–0xFF | 无 | 软件 `int n` 或异常情况：按 fatal 处理 |

- **为什么 remap**：BIOS 默认把 IRQ0–7 放在 0x08–0x0F，与 #DF、#GP、#PF 等 CPU 异常重叠，无法区分定时器中断和 double fault。
- `pic_init()`（drivers/pic.c）：先写 OCW1 0xFF 屏蔽两片 PIC，再发 ICW1–ICW4（向量基址 0x20/0x28，从片接在 IRQ2，8086 模式）。
  初始化会清空 IMR，所以结束时再次写 0xFF。整个过程 IF=0。
- 只放行 IRQ0（PIT）和 IRQ1（键盘）。从片和 cascade（IRQ2）保持屏蔽，因为本阶段没有从片设备。
  启动时读回硬件 IMR 并打印：`PIC_OK vectors=0x20/0x28 mask=0xfc/0xff`。
- 若以后启用从片 IRQ，`pic_unmask()` 会同时打开 IRQ2；EOI 顺序是**先从片后主片**（`pic_send_eoi()`）。

## 2. IRQ 路径与 EOI 顺序

```
CPU：压 EFLAGS/CS/EIP，清 IF（中断门）
 └ isr_stub_0x20..0x2F：push 0（无错误码），push vector
    └ isr_common：pushad、保存段寄存器、cld、对齐栈，call isr_dispatch(frame)
       └ isr_dispatch：vector ∈ [0x20, 0x30) → irq_dispatch(vector - 0x20)
          ├ depth++、记录 max_depth；若 IF=1 则 if_in_handler++（不应发生）
          ├ IRQ7/15：读 ISR，若对应位为 0 → spurious++，不发 EOI（IRQ15 只给主片发）→ 返回
          ├ count[irq]++
          ├ handlers[irq]()            ← 设备处理：读端口、更新计数、入队
          ├ pic_send_eoi(irq)          ← 处理完设备之后发 EOI
          ├ eoi[irq]++，depth--
          └ 返回
       └ isr_common：mov esp, ebx；恢复段寄存器；popad；add esp, 8；iretd
CPU：iretd 弹出 EIP/CS/EFLAGS → IF、DF 等恢复为被中断时的值
```

- **EOI 的作用**：告诉 PIC 当前中断已经处理完毕，清除其 ISR 位；否则 PIC 认为该级别仍在服务中，不再递送同级或更低优先级的 IRQ。
- EOI 在 `iretd` 之前发出，但此时 IF 仍为 0，所以 PIC 即使立刻提出下一个 IRQ，CPU 也要等 `iretd` 恢复 IF=1 之后才接收，不会在处理函数内部嵌套。
  测试中 `maxdepth` 始终为 1，`irqN == eoiN`。
- 处理函数里**不执行 `sti`**。IF 只在 `kernel_main` 里打开一次（`irq_enable()`，所有入口、处理函数和队列准备好之后），
  之后由 `iretd` 按被中断代码保存的 EFLAGS 恢复。
- 返回后，寄存器、栈指针（frame + 0x44）和 EFLAGS 与中断前完全一致；IRQ 返回自检逐次验证（第 6 节）。
- IRQ 处理函数不打印、不分配内存、不切换任务、不解析命令；诊断与统计都在主循环中读取并打印。
- 屏蔽线上的 IRQ 或没有注册处理函数的 IRQ：计入 `unexpected`，同样发 EOI 并返回，不会 panic 或重启；数值出现在每秒的 TICKS 状态行里。

## 3. PIT（IRQ0）

- 通道 0，模式 2（rate generator），先低后高字节，除数 `(1193182 + 50) / 100 = 11932`，名义频率约 99.998 Hz。
- IRQ0 处理函数只做 `pit_ticks++`。
- **tick 接口**（include/rangeros/pit.h）：32 位无符号数，从 `pit_init()` 起计；100 Hz 下约 497 天回绕一次。
  比较必须使用 `tick_reached(now, target)`，即 `(int32_t)(now - target) >= 0`，在两值相差小于 2^31 时跨回绕仍然正确。
  回绕边界测试计划在 M4b（初值接近上限）；M2 只验证单调递增。
- tick 是内核的模拟时间基准，不宣称硬件实时精度；测试不以虚拟机墙钟判定频率是否合格。

## 4. 键盘（IRQ1）

### 4.1 扫描码集合与 QEMU 配置

- QEMU `pc` 机型默认的 i8042 控制器加 PS/2 键盘；本项目没有传入任何 USB 或输入设备参数（`-nic none`，没有 `-usb`）。
- 键盘本身发出 set 2，i8042 开启 **translation**（配置字节第 6 位）后转换为 **set 1** 交给 CPU。驱动依赖 set 1。
- `keyboard_init()`：清空输出缓冲区；读配置字节（命令 0x20）；置位 IRQ1 使能和 translation，清除 IRQ12 使能与第一端口时钟禁用位；如有变化则写回（命令 0x60）；再次清空缓冲区；注册 IRQ1。
  实测配置字节为 0x61：translation=1、IRQ1=1，第 5 位为第二端口时钟禁用。
- 清空缓冲区时 PIC 可能已经锁存一次 IRQ1 边沿，所以解除屏蔽后会收到一次**没有数据的 IRQ1**。
  处理函数检查状态寄存器第 0 位，没有数据就记 `empty` 并返回（实测每次启动都是 `kbd_empty=1`、`kbd_scancodes=0`）。
- 鼠标数据（状态第 5 位）读出后丢弃，不计入扫描码。

### 4.2 解码（lib/kbd_decode.c，纯逻辑，有宿主单测）

| 输入 | 行为 |
|---|---|
| 字母 make 码 | `a`–`z`；任一 Shift 按下时为 `A`–`Z` |
| 数字 0x02–0x0B | `1`–`0`；Shift 时为 `!@#$%^&*()` |
| 0x39 / 0x1C / 0x0E | 空格 / `'\n'` / `'\b'` |
| 0x2A / 0x36 及其 break 码 | 分别记录左右 Shift 的按下与释放；两个 Shift 独立 |
| 其他 break 码（第 7 位为 1） | 忽略 |
| `0xE0` 前缀 | 忽略**下一个字节**（扩展键的 make 或 break）。所以 PrintScreen 的伪 Shift `E0 2A` 不会改变 Shift 状态 |
| `0xE1`（Pause） | 忽略该序列余下的 5 个字节 |
| 0x00、0xEE、0xFA、0xFE、0xFF | 控制器或键盘应答、错误码，忽略 |
| 其他 make 码（F 键、Ctrl、Alt、Caps、符号键…） | 忽略 |

不支持：Caps Lock、Ctrl/Alt 组合键、方向键、小键盘、符号键、键盘 LED 和 typematic 设置。

### 4.3 IRQ1 处理

每次 IRQ1 只读一个字节：`irqs++`；若没有数据则 `empty++`；否则 `scancodes++`，交给解码器（Shift 状态就在这里维护）；
得到字符就入队（成功 `chars++`，队列满 `dropped++`），否则 `ignored++`。不打印。

## 5. 输入队列与同步

- `struct input_queue`（lib/input_queue.c）：固定 128 字节环形缓冲，`head` 指向最旧字节，`count` 区分空（0）和满（128）。
  满时**丢弃新字节**、`dropped++`，已入队的内容不会被覆盖。纯逻辑，有宿主单测。
- 写者：IRQ1 处理函数（IF=0）。读者：主循环，通过 `keyboard_read()` / `keyboard_pending()` 在 `irq_save()` / `irq_restore()` 之间访问。
- **临界区**（arch/i386/irqflags.h）：`irq_save()` 保存 EFLAGS 后 `cli`；`irq_restore()` 用 `popfl` 写回保存的 EFLAGS，因此 IF 恢复为**进入前的值**，可以嵌套。
  两者都带 `"memory"` clobber，充当编译器屏障。统计快照（`irq_stats_snapshot()`、`keyboard_stats_snapshot()`）同样在临界区里整体复制，保证各字段一致。
- **为什么 volatile 不够**：volatile 只阻止编译器缓存单个变量，不能让 `head`/`count`/`buf[]` 这样的多字段更新相对 IRQ 成为原子操作。
  例如主循环读到新的 `count` 却读到旧的 `buf` 槽，或者 pop 的读-改-写被 IRQ 打断。必须关中断，并加编译器屏障。
- 唯一的例外是 IRQ 返回自检探针（测试代码），它用一条对齐的 32 位读取 `pit_ticks`，这在 i386 上不会撕裂。

## 6. 主循环与 idle

> M3 起，主循环把每个字节交给 shell（`shell_input()`），等待改用 kernel/wait.c 的 `wait_for_input()`，契约相同（见 [shell.md](shell.md) §4）。下面的代码描述的是 M2 时的形态。

```c
for (;;) {
    while ((c = keyboard_read()) >= 0) handle_input(c);   // 回显到 VGA，串口每字节一行 KEY
    if (tick_reached(timer_ticks(), next_report)) report_ticks(...);  // 每 100 tick 一行 TICKS（仅串口）
    wait_for_interrupt();
}
```

`wait_for_interrupt()`：`cli` → 检查队列 → 有输入则 `sti` 后返回；否则执行 `sti; hlt`。
`sti` 的效果要到下一条指令之后才生效，所以检查之后才到达的 IRQ 也会唤醒 `hlt`，不会丢失唤醒。IRQ0 始终开启，保证 `hlt` 总有唤醒源（SPEC S04）。

M2 主循环只回显，不做行缓冲，也不解析命令（属于 M3 shell）。串口输出格式：
`KEY 0x48 'H'`、`KEY 0x20 SPACE`、`KEY 0x08 BACKSPACE`、`KEY 0x0a ENTER`，每次 ENTER 之后跟一行 `KBD chars=.. scancodes=.. dropped=.. ignored=..`。

## 7. 测试入口

| 目的 | 方式 |
|---|---|
| 队列空、满、回绕、dropped | tests/host/test_input_queue.c（宿主 gcc） |
| 扫描码解码 | tests/host/test_kbd_decode.c |
| PIT 持续到达、EOI、无嵌套 | 正常内核的 TICKS 行 + tests/scripts/check_irq.py |
| IRQ 返回保持现场 | selftest-irqret：tests/kernel/irqret_probe.asm 在 IF=1 下自旋 150 tick，每轮校验 6 个寄存器的固定值、ESP、4 个栈哨兵、DF=1、IF=1，以及 EAX 上的 LCG 计算结果 |
| 自检能否发现错误 | selftest-irqret-sabotage：用 `-DRANGEROS_IRQ_RETURN_SABOTAGE` 汇编的 isr.o 在返回时破坏 ESI，探针必须报告 code=4 |
| PS/2 路径 | QEMU monitor `sendkey`（经 QEMU 的 PS/2 键盘模型产生真实的 i8042 扫描码和 IRQ1；COM1 没有输入）+ check_kbd.py + VGA 文本缓冲区 |
