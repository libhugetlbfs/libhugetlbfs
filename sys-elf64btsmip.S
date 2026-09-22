/*
 * libhugetlbfs - direct system call for mips64 (n32 and n64)
 *
 * Copyright (C) 2015 AudioCodes Ltd.
 *
 * Based on code from the GNU C Library, Copyright Free Software
 * Foundation, Inc.
 */
	.text

	/* No floating point here, so do not force an FP ABI on the
	 * library: without this the object is tagged hard-float and
	 * a soft-float toolchain warns when it links. */
	.gnu_attribute 4, 0

/*
 * long direct_syscall(sysnum, arg1, ..., arg7)
 *
 * The syscall number arrives in a0 and has to move to v0, so every
 * argument shifts down one register.  n32 and n64 both pass eight
 * arguments in registers, which covers the seven a syscall can take.
 */
	.globl	direct_syscall
	.ent	direct_syscall
direct_syscall:
	move	$2, $4		/* v0 = syscall number */
	move	$4, $5		/* a0 = arg1 */
	move	$5, $6
	move	$6, $7
	move	$7, $8
	move	$8, $9
	move	$9, $10
	move	$10, $11
	syscall
	j	$31
	.end	direct_syscall

#if defined(__linux__) && defined(__ELF__)
	.section .note.GNU-stack,"",%progbits
#endif
