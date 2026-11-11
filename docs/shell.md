# Shell 设计（M3，MVP）

中断与输入队列见 [interrupts.md](interrupts.md)；证据见 [evidence/M3.md](evidence/M3.md)。

## 1. 输入路径（只有一条）

```
PS/2 键盘 → i8042（set 1）→ IRQ1：读字节、解码（Shift 状态）、入队 128 字节队列、计数、EOI
                                    （不打印、不解析命令）
主循环（IF=1）：keyboard_read() 取出字节 → shell_input(c)
                → line_edit_feed()（127 字符行缓冲）→ 回显到 VGA
                → Enter：execute()，命令输出到 VGA 与串口
```

- 交互输入**只**来自 PS/2 队列。没有串口输入适配器：`make run`（-nographic）终端中输入的字符进入 COM1，内核不读取。
- 自动化测试通过 QEMU monitor 的 `sendkey` 驱动 QEMU 的 PS/2 键盘模型，走的就是上面这条路径。测试不使用另一套解析代码。
- 测试专用代码只有一个阶段性钩子（selftest 内核，见第 5 节）；`build/kernel.elf` 中没有测试符号（M2 起由用例检查）。

## 2. 行编辑（lib/line_edit.c，纯逻辑，有宿主单测）

| 输入 | 行为 | VGA |
|---|---|---|
| 0x20–0x7E 且 len < 127 | 存入 `buf[len++]`，`buf[len] = '\0'` | 回显 |
| 0x20–0x7E 且 len = 127 | 拒收，`rejected++`，不写入缓冲区 | 无 |
| `'\b'` 且 len > 0 | 删除最后一个字符 | 退格并擦除 |
| `'\b'` 且 len = 0 | 不做任何事 | 无 |
| `'\n'` | 提交 | 换行 |
| 其他字节 | 忽略 | 无 |

- 缓冲区 `char buf[128]`：127 个字符加终止符；`len` 永远 ≤ 127，`buf[len]` 永远是 `'\0'`。宿主测试在结构体前后放了保护字节，验证 500+ 个超长字符不会越界写入。
- **超长行**：只要 `rejected > 0`，提交时打印 `shell: line too long (max 127 chars), N rejected; discarded`，**不执行**。
  这样可以避免把被截断的输入当成命令运行。即使之后用退格缩短了这一行，结果也一样；按 Enter 之后状态清零。
- 空行（或只有空格）：不输出任何内容，直接显示新的提示符。

## 3. 命令

解析：去掉首尾空格，第一个词是命令名，其余部分是参数。当前命令都不接受参数，带参数时打印 `shell: <cmd> takes no arguments`。
找不到命令时打印 `unknown command: <word> (type help)`。shell 的错误信息都以 `shell:` 开头，不用 `error:`，因为测试运行器把 `^error: ` 当作 GRUB 错误。

| 命令 | 输出 |
|---|---|
| `help` | 命令列表与一行说明 |
| `version` | `RangerOS 0.1.0-mvp (i386, multiboot1)` 与 MVP 功能列表 |
| `ticks` | `ticks=N hz=100 uptime=S.CCs (simulated)` |
| `clear` | 清空 VGA（串口记录一行 `CLEAR vga`） |
| `demo` | 5 步，每步等待 20 tick（`wait_until_tick`，用 hlt 而不是忙等），打印 tick、扫描码数和队列长度，共约 100 tick |

`demo` 运行期间主循环被占用，但 IRQ1 照常接收：期间键入的字符留在队列里，demo 结束后再处理（测试中可以看到 `queued=2→6`）。它不引入任务调度。

## 4. 等待中断（kernel/wait.c）

```
cli
if (条件已满足) { sti; return; }      // 有输入 / tick 已到
sti; hlt                              // sti 要到 hlt 之后才生效
```

- 检查和进入休眠之间没有空窗：IRQ 如果在 `cli` 之后到达，会一直挂起，直到 `sti; hlt` 这一对指令执行完；`hlt` 随即被唤醒，不会丢失唤醒。
- 契约：只能在主循环上下文中调用，调用时 IF=1，返回时 IF=1；如果以 IF=0 进入，直接 panic（因为末尾一定会 `sti`）。不能在 IRQ 处理函数或临界区里调用。
- IRQ0 始终开启，所以 `hlt` 最多睡一个 tick（10 ms）。
- `wait_for_input()` 用于主循环空闲；`wait_until_tick()` 用于 `demo` 和 queue-full 自检。

## 5. 输出与日志

- **VGA**：提示符 `> `、回显、命令输出。
- **COM1**：命令输出，以及诊断日志：
  - `KEY 0x.. …`：每个从队列取出的字节一行；
  - `SHELL len=N rejected=R [行内容]`：每次提交一行，方括号保留行尾空格；
  - `KBD chars=.. scancodes=.. dropped=.. ignored=..`：每次提交之后一行；
  - `TICKS …`：每 100 tick 一行（M2）。

  所有行都由主循环整行输出，IRQ 不输出，所以各行不会交错。
- queue-full 自检（tests/kernel/selftest_qfull.c，仅测试内核）：在 `SELFTEST_STAGE_IRQ_ON` 阶段等待按键（IF=1、IRQ1 照常接收，但还没有消费者），然后返回到正常的 `shell_init()` 与主循环。
  所以恢复之后走的是正式 shell 路径。

## 6. 不在 MVP 内

命令历史、光标移动、Tab 补全、命令参数、Ctrl 组合键、多行输入、串口输入、调度（M4a/M4b）、分页、用户态、文件系统。
