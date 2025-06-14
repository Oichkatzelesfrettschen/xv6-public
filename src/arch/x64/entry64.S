#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"

# 64-bit segment descriptor helper provided by asm.h

.p2align 2
.globl _start
_start = V2P_WO(entry64)

.globl entry64
entry64:
    lgdt gdtdesc
    movw $(2<<3), %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    mov $stack64 + KSTACKSIZE, %rsp
    mov $main64, %rax
    jmp *%rax

.comm stack64, KSTACKSIZE

# 64-bit GDT for the kernel
.p2align 2
gdt:
    SEG_NULLASM
    SEG_ASM64(STA_X|STA_R,0,0xffffffff)  # code
    SEG_ASM64(STA_W,0,0xffffffff)        # data

gdtdesc:
    .word (gdtdesc - gdt - 1)
    .long gdt
    .long 0

# 64-bit system call entry. Saves registers in a trap frame and
# invokes the common trap handler. This implementation is minimal
# and omits some details such as saving the user stack pointer.

.globl syscallentry
syscallentry:
    swapgs
    lfence
    # push general registers
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rbx
    pushq %rdx
    pushq %rcx
    pushq %rax
    # trap number and minimal frame information
    pushq $T_SYSCALL
    pushq $0          # dummy error code
    pushq %rcx        # saved RIP from RCX
    pushq $(SEG_KCODE<<3) # CS = 0x08, kernel code segment
    pushq %r11        # EFLAGS from R11
    pushq %rsp        # current RSP (approximate)
    pushq $(SEG_KDATA<<3) # SS = 0x10, kernel stack segment
    # Trap frame layout now matches struct trapframe in trap.c:
    # r15..r8, rdi, rsi, rbp, rbx, rdx, rcx, rax,
    # trapno, err, rip, cs, eflags, rsp, ss

    movq %rsp, %rdi   # argument: pointer to trap frame
    lfence
    call trap
    jmp trapret

.globl trapret
trapret:
    addq $7*8, %rsp    # skip ss,rsp,eflags,cs,rip,err,trapno
    addq $8, %rsp      # drop saved rax
    popq %rcx
    popq %rdx
    popq %rbx
    popq %rbp
    popq %rsi
    popq %rdi
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    lfence
    swapgs
    lfence
    iretq
