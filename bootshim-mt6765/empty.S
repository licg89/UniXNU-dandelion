.section .text.head, "ax"
.global _head
_head:
    b _start
    .long 0
    .quad 0
    .quad _end
    .quad 0xa
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0

.text
.global _start
_start:
    /* Minimal test: just infinite loop */
    /* If LK can load this, device will stop rebooting */
    b _start

.global _end
_end:
