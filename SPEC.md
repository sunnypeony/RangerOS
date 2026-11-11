# RangerOS — 设计约束与验收规格 v1.0

状态：设计已确定；实现与测试均未开始。日期按 2026 年、America/Los_Angeles。
本文件规定功能及技术边界；MILESTONES.md 规定日期与验收证据。不得把设计目标写成已完成成果。

## 1. 目标与交付等级

实现一个可启动的 32 位 x86 教学内核，理解异常、中断、独立任务栈和调度。

| 等级 | 必须具备 | 交付定位 |
|---|---|---|
| MVP | 启动、VGA/串口、异常诊断、PIT、键盘、shell | 11/23 冻结的可提交版本 |
| Standard | 固定任务槽、独立栈、协作式 RR、sleep、exit、tasks | 12/07 前争取完成 |
| Stretch | 定时器抢占，且验证临界区与恢复路径 | 不承诺在课程内完成 |

不实现：用户态、地址空间隔离、syscall、文件系统、网络、USB、真实传感器、SMP、动态任务栈回收。Paging/heap 不属于本轮等级，不因调度需要而自动扩张。所有任务 ring 0、共享地址空间，报告称“内核线程”，不称隔离进程或实时 OS。

## 2. 平台与构建约束

- Linux 主机；M0 记录发行版、uname -m、工具版本。宿主非 x86 时重新明确工具链，不盲用 -m32。
- QEMU PC、单 CPU、128 MiB RAM；C + NASM；GRUB Multiboot 1、ELF32 i386 内核、ISO。QEMU 使用软件模拟即可，不要求 KVM。
- 主线只维护 GRUB 启动；自写 boot sector 是独立可选实验，不阻塞任何里程碑。
- 先使用可工作的宿主 GCC -m32；编译/链接分别处理 freestanding、PIE/PIC、栈保护、宿主启动文件和库依赖。禁用浮点/SIMD 使用，不引入相应上下文保存。
- M0 用 readelf、nm、Multiboot 检查确认：ELF32/i386、无动态解释器、无意外未解析符号、入口与段布局正确、Multiboot 头可识别。不能只以编译返回 0 判成功。
- 无法稳定构建时切换固定版本的预制 i686-elf 环境；不把从源码构建工具链设为前置任务。
- 输出 build/kernel.elf（调试符号）、build/rangeros.iso、链接 map。make clean 后可以重建。记录工具版本即可，不要求字节完全一致的可复现构建。
- 本地 make test 优先；CI 后置。测试专用 QEMU 退出设备不得进入正常 shell 关机路径。

### 2.1 实际工具链配置（M0 前环境核对，已人工验证）

本节只记录构建环境，不改变第 1 节功能范围。宿主为非 x86，故按上文规则重新明确工具链，不使用宿主 gcc -m32。

- 宿主：Mac 上 UTM 虚拟机内的 Ubuntu 20.04.6 LTS，uname -m = aarch64。所有编译、链接、GRUB 打包、QEMU 与测试均在该虚拟机执行。
- C：clang 10（--target=i686-none-elf），freestanding、-fno-pic/-fno-pie、-fno-stack-protector，禁用 MMX/SSE/x87。
- 链接：ld.lld 10（-m elf_i386，自有链接脚本，-nostdlib，无宿主启动文件与库）。
- 汇编：NASM 2.14（-f elf32）。
- ELF 检查：GNU readelf/nm 2.34（可读 i386 ELF）；grub-file --is-x86-multiboot 检查 Multiboot 头。
- GRUB：宿主 grub-common 2.04 的 grub-mkrescue，配合 -d ~/projects/RangerOS/tools/grub/extracted/usr/lib/grub/i386-pc（由 grub-pc-bin_2.04-1ubuntu26.17_amd64.deb 解包；宿主 apt 不提供 i386-pc 模块）。
- QEMU：qemu-system-i386 4.2.1，-accel tcg、-smp 1、-m 128M；交互运行用 -nographic 查看串口，不依赖图形窗口。
- env-check/ 中的 clang 编译检查与 GRUB ISO 启动检查已人工通过；保留该目录与 tools/ 作为环境证据。

## 3. 模块与初始化顺序

目录：arch/i386（入口、GDT/IDT、ISR、端口、switch）；drivers（VGA、COM1、PIC、PIT、键盘）；kernel（初始化、panic、task、scheduler）；lib/include；shell；tests/scripts；docs。

初始化顺序：关闭 IRQ → 初始栈及 C 环境（含 BSS）→ 串口/VGA → 自有 GDT → IDT/异常 → PIC remap 且屏蔽 IRQ → PIT/键盘及队列 → 清理/应答必要状态 → 仅放行已支持 IRQ → 开中断 → 主循环或任务运行时。

约束 D01：中断未准备好之前不执行 sti；入口清方向标志，调用 C 遵守约定的 ABI 与栈对齐。栈布局在 docs/context.md 画明偏移。
约束 D02：PIC 映射为 0x20/0x28，异常占 0–31。仅开放 IRQ0/IRQ1；若后续支持 slave IRQ，明确 cascade 与 EOI 顺序。未知 IRQ 不应造成无日志重启。
约束 D03：异常入口统一 vector/error-code 布局，区别 CPU 自动压入与人工补零。诊断包含 vector、error、EIP；仅 page fault 才把 CR2 当故障地址解释。panic 禁止继续调度。
约束 D04：PIT 目标 100 Hz；tick 是内核模拟时间基准，不宣称硬件实时精度。tick 位宽、回绕比较规则写进接口；不以墙钟精度作为通过条件。

## 4. 输入与交互

- PS/2 键盘范围：字母、数字、空格、Enter、Backspace；至少支持基本 Shift，其他扫描码安全忽略。明确目标扫描码集合和 QEMU 配置。
- IRQ 只读取设备、解析必要状态、入队、计数及 EOI；不运行 shell、分配内存或做长打印。
- 固定容量输入队列 128 项；满时丢新输入，增加 dropped 计数。空/满/回绕必须测试。
- 单 CPU 下，用保存并恢复原 IF 的短临界区保护共享队列与调度结构；volatile 不能代替同步。禁止在普通临界区内 yield/sleep。
- 命令行上限 127 字符（另留终止符）；超长输入拒收额外字符但保持终止符，退格与空行安全。
- 命令：help、version、ticks、clear、demo；Standard 增加 tasks。未知命令报告错误后继续可用。
- MVP 主循环消费输入；Standard shell 无输入时主动 yield。WAIT_INPUT 阻塞是可选扩展，若实现，检查条件与进入等待必须原子化；IRQ 只使其 READY，不直接切任务。
- 自动化可用独立 serial 测试输入适配器，但不能用它冒充 PS/2 路径通过；键盘至少有 QEMU 注入测试或明确的人工验收记录。

## 5. 任务与调度

固定 8 个任务槽（含 idle），每栈 16 KiB、16 字节对齐，静态存储。使用栈哨兵发现越界迹象；无分页时不宣称有 guard page。满表创建返回错误，不覆盖任务。

状态：READY、RUNNING、SLEEPING、FINISHED；unused 用独立占用标记或 UNUSED。控制块至少包含 id/name/state/saved_sp/wake_tick/switch_count/栈范围。

接口：task_create(name, entry, arg)、task_yield()、task_sleep_ticks(n)、task_exit()。sleep(0) 等价 yield。函数自然返回通过 trampoline 调用 task_exit，不能落到未知返回地址。FINISHED 不再调度；本轮不回收槽位。

约束 S01：单核 Round-Robin，每次选择下一个 READY 任务；不存在时选择 idle。task_sleep_ticks 按 tick 唤醒；不以 busy loop 模拟 sleep。
约束 S02：每任务独立栈。使用一个底层 switch_context，按 ABI 保存 callee-saved 寄存器、栈指针和返回路径。文档区分 context 与 trapframe；中断现场留在所属任务栈，不能被另一任务覆盖。
约束 S03：首次进入任务的栈由 trampoline 契约定义，明确返回地址、参数、IF 状态和对齐；恢复任务保持其约定的中断状态。不可无条件 sti 破坏调用者状态。
约束 S04：idle 安全等待 IRQ；检查 READY 与进入休眠须防竞态。说明 cli/检查/sti-hlt 的使用条件，保证存在已启用的唤醒源。idle 不阻塞其他 READY 任务。
约束 S05：协作式内核允许任务在两次 yield 之间占用 CPU；不把此行为当 bug，也不声称已抢占。

### Stretch 的额外门槛

可复用 switch_context，但必须保留完整中断入口/返回链。切换前按约定发 EOI，避免 PIC 认为 IRQ 一直未完成。只在允许抢占的点调度；临界区和中断嵌套策略明确。串口/VGA 不可因中断打断重入而死锁或破坏结构。使用测试计数器而非大量中断打印证明进展。新任务及恢复路径的 IF、iret 路径有专门测试。不得仅增加 timer ISR 中一行 yield 就宣称通过。

## 6. 验收证据规则

- 每个测试有明确输入、预期结果、超时、退出状态、日志路径；缺日志或无法执行标为 NOT RUN，不写 PASS。
- 默认 QEMU 集成用例超时 15 秒，可对长调度测试单独设 30 秒并记录。超时、意外退出/重启、缺标记、错误计数非零均失败。
- isa-debug-exit 是可选测试出口；脚本必须解码其退出约定，不能要求 QEMU 原始退出码为 0。看到 PASS 文本仍需检查退出状态。
- 主机测试验证纯逻辑；QEMU 测试验证启动、IRQ、栈切换。两者不能互相替代。
- 每阶段保留源码提交标识、命令、结果、工具版本及限制。截图必须来自真实运行，不制作效果图。

## 7. 执行与变更控制

Codex 每次只实现一个里程碑；附改动摘要、实际命令、结果、日志、已知限制及三个学习问题。核心 ISR/switch 汇编要求逐行说明和栈图，用户走读并做一次受控修改；不以手敲行数衡量理解。

不自动增加分页、文件系统或传感器应用。新功能须更新规格，列出占用时间和被替代工作；不得侵占冻结日期。课程 AI 使用政策目前未核实，不作允许或禁止的结论；来源和辅助方式如实记录，按课程要求提交。

参考：littleosbook https://littleosbook.github.io/ （Ch.2–6 主线，Ch.14 调度参考）；x86 xv6 https://github.com/mit-pdos/xv6-public （swtch.S、trapasm.S、trap.c、proc.c）；Blundell PDF 仅作启动补充。第三方代码保留许可与出处。cloc 统计与作者身份分开说明。
