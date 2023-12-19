/*
 * libhugetlbfs - direct system call for loong64
 *
 * Author(s): WuRuilong
 */
	.text

	.globl	direct_syscall
direct_syscall:
        move	  $a7, $a0
        move	  $a0, $a1
	move      $a1, $a2
        move      $a2, $a3
        move      $a3, $a4 
        move      $a4, $a5
	move      $a5, $a6
        syscall 0
	ret

#if defined(__linux__) && defined(__ELF__)
	.section .note.GNU-stack,"",%progbits
#endif
