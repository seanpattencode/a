.global _start
.section .text
_start:
    mov $1, %rax        # syscall: write
    mov $1, %rdi        # fd: stdout
    lea msg(%rip), %rsi # buf
    mov $6, %rdx        # len
    syscall
    mov $60, %rax       # syscall: exit
    xor %rdi, %rdi      # status: 0
    syscall
.section .rodata
msg: .ascii "hello\n"
