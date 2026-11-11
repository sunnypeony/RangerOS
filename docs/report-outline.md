# 一页报告提纲（MVP，v0.1-mvp）

MILESTONES M3 要求的提纲。正文在提交阶段（12/08–12/15）完成；数字取自证据文件，提交前须按最终版本重新核对。

1. **目的**：在 QEMU PC 上实现可启动的 32 位 x86 教学内核，理解启动、异常、中断与输入。等级：MVP（SPEC §1）。
2. **平台与构建**：aarch64 Ubuntu 虚拟机，使用 clang/ld.lld 交叉编译到 i386，NASM，GRUB Multiboot 1 ISO，QEMU TCG（SPEC §2.1）。
3. **架构**（一张图）：boot.asm → kernel_main 初始化顺序 → GDT/IDT → PIC/PIT/键盘 → sti → 主循环与 shell；IRQ 路径与 EOI；异常的致命诊断路径。
4. **实际功能**：串口与 VGA 输出；#UD/#GP 诊断；100 Hz tick；PS/2 键盘（set 1）；128 字节队列；shell 的 5 个命令与 127 字符行。不包含：调度、分页、用户态。
5. **验证方法**：宿主单测与 QEMU 集成测试分开；运行器解码退出码（PASS/TIMEOUT/EARLY_EXIT/FAIL_MARKER/ERROR），不信任 QEMU 自身的退出码；
   每个检查器都有负向用例；VGA 从显存读取；键盘经 QEMU 的 PS/2 模型注入。代表性结果：M0–M3 共 59 个用例；2 分钟稳定性运行；IRQ 返回现场探针及其破坏版负向用例。
6. **截图**：启动、#GP 诊断、shell 会话、队列满之后恢复（docs/evidence/M*-logs/*.png）。
7. **限制**：尚无任务调度；没有栈哨兵；没有 spurious IRQ 与 tick 回绕的实测；键位有限；QEMU 环境以外未验证（见各 M*.md）。
8. **代码规模**：cloc 统计，排除 build/、tools/ 和生成物；测试代码与内核代码分列。行数不作为原创证明（MILESTONES 提交整理）。
9. **来源与辅助说明**：参考资料（littleosbook、xv6 等）；AI 辅助的方式如实记录，按课程政策提交（SPEC §7；政策尚未核实）。
