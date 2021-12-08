/*
 * libhugetlbfs - direct system call for loongarch64
 *
 * Author(s): Zhang Wenlong
 */


	.text

	.globl	direct_syscall

direct_syscall:
	move	$a0,$ra
	move	$a1,$a0
	move	$a2,$a1
	move	$a3,$a2
	move	$a4,$a6
	move	$a5,$a4
	move	$fp,$a5
	syscall	0
	jr	$ra
#if defined(__linux__) && defined(__ELF__)
	.section .note.GNU-stack,"",%progbits
#endif
