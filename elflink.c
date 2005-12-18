#define _GNU_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <elf.h>
#include <dlfcn.h>

#include "hugetlbfs.h"
#include "libhugetlbfs_internal.h"

#ifdef __LP64__
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Phdr	Elf64_Phdr
#else
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Phdr	Elf32_Phdr
#endif

#ifdef __i386__
/* The normal i386 syscall macros don't work with -fPIC :( */
#undef _syscall2
#define _syscall2(type,name,type1,arg1,type2,arg2) \
type name(type1 arg1,type2 arg2) \
{ \
long __res; \
__asm__ volatile ("push %%ebx; movl %2,%%ebx; int $0x80; pop %%ebx" \
        : "=a" (__res) \
        : "0" (__NR_##name),"r" ((long)(arg1)),"c" ((long)(arg2))); \
__syscall_return(type,__res); \
}
#undef _syscall3
#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3) \
type name(type1 arg1,type2 arg2,type3 arg3) \
{ \
long __res; \
__asm__ volatile ("push %%ebx; movl %2,%%ebx; int $0x80; pop %%ebx" \
        : "=a" (__res) \
        : "0" (__NR_##name),"r" ((long)(arg1)),"c" ((long)(arg2)), \
                  "d" ((long)(arg3))); \
__syscall_return(type,__res); \
}
#endif /* __i386__ */

#define MAX_HTLB_SEGS	2

static struct seg_info htlb_seg_table[MAX_HTLB_SEGS];
static int htlb_num_segs;

static void parse_phdrs(Elf_Ehdr *ehdr)
{
	Elf_Phdr *phdr = (Elf_Phdr *)((char *)ehdr + ehdr->e_phoff);
	int i;

	for (i = 0; i < ehdr->e_phnum; i++) {
		unsigned long vaddr, filesz, memsz;
		int prot = 0;

		if (phdr[i].p_type != PT_LOAD)
			continue;

		if (! (phdr[i].p_flags & PF_LINUX_HUGETLB))
			continue;

		if (htlb_num_segs >= MAX_HTLB_SEGS) {
			ERROR("Executable has too many segments marked for "
			      "hugepage (max %d)\n", MAX_HTLB_SEGS);
			htlb_num_segs = 0;
			return;
		}

		vaddr = phdr[i].p_vaddr;
		filesz = phdr[i].p_filesz;
		memsz = phdr[i].p_memsz;
		if (phdr[i].p_flags & PF_R)
			prot |= PROT_READ;
		if (phdr[i].p_flags & PF_W)
			prot |= PROT_WRITE;
		if (phdr[i].p_flags & PF_X)
			prot |= PROT_EXEC;

		DEBUG("Hugepage segment %d (phdr %d): %lx-%lx  (filesz=%lx)\n",
		      htlb_num_segs, i, vaddr, vaddr+memsz, filesz);

		htlb_seg_table[htlb_num_segs].vaddr = (void *)vaddr;
		htlb_seg_table[htlb_num_segs].filesz = filesz;
		htlb_seg_table[htlb_num_segs].memsz = memsz;
		htlb_seg_table[htlb_num_segs].prot = prot;
		htlb_num_segs++;
	}
}

static int prepare_segments(struct seg_info *seg, int num)
{
	int hpage_size = gethugepagesize();
	int i;
	void *p;
	
	/* Prepare the hugetlbfs files */
	for (i = 0; i < num; i++) {
		int fd;
		unsigned long copysize = seg[i].memsz;
		unsigned long size = ALIGN(seg[i].memsz, hpage_size);

		fd = hugetlbfs_unlinked_fd();
		if (fd < 0) {
			ERROR("Couldn't open file for segment %d: %s\n",
			      i, strerror(errno));
			return -1;
		}

		seg[i].fd = fd;

		p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED) {
			ERROR("Couldn't map hugepage segment to copy data\n");
			return -1;
		}
		
		DEBUG("Mapped hugeseg %d at %p. Copying %ld bytes from %p...",
		      i, p, copysize, seg[i].vaddr);
		memcpy(p, seg[i].vaddr, copysize);
		DEBUG_CONT("done\n");

		munmap(p, size);
	}

	return 0;
}

/* This function prints an error message to stderr, then aborts.  It
 * is safe to call, even if the executable segments are presently
 * unmapped.
 *
 * Arguments are printf() like, but at present supports only %d and %p
 * with no modifiers
 *
 * FIXME: This works in practice, but I suspect it
 * is not guaranteed safe: the library functions we call could in
 * theory call other functions via the PLT which will blow up. */
#define __NR_sys_write __NR_write
#define __NR_sys_getpid __NR_getpid
#define __NR_sys_kill __NR_kill
static _syscall3(ssize_t,sys_write,int,fd,const void *,buf,size_t,count);
static _syscall0(pid_t,sys_getpid);
static _syscall2(int,sys_kill,pid_t,pid,int,sig);
static void write_err(const char *start, int len)
{
	sys_write(2, start, len);
}
static void sys_abort(void)
{
	pid_t pid = sys_getpid();

	sys_kill(pid, SIGABRT);
}
static void write_err_base(unsigned long val, int base)
{
	const char digit[] = "0123456789abcdef";
	char str1[sizeof(val)*8];
	char str2[sizeof(val)*8];
	int len = 0;
	int i;

	str1[0] = '0';
	while (val) {
		str1[len++] = digit[val % base];
		val /= base;
	}

	if (len == 0)
		len = 1;

	/* Reverse digits */
	for (i = 0; i < len; i++)
		str2[i] = str1[len-i-1];

	write_err(str2, len);
}

static void unmapped_abort(const char *fmt, ...)
{
	const char *p, *q;
	int done = 0;
	unsigned long val;
	va_list ap;

	/* World's worst printf()... */
	va_start(ap, fmt);
	p = q = fmt;
	while (! done) {
		switch (*p) {
		case '\0':
			write_err(q, p-q);
			done = 1;
			break;

		case '%':
			write_err(q, p-q);
			p++;
			switch (*p) {
			case 'u':
				val = va_arg(ap, unsigned);
				write_err_base(val, 10);
				p++;
				break;
			case 'p':
				val = (unsigned long)va_arg(ap, void *);
				write_err_base(val, 16);
				p++;
				break;
			}
			q = p;
			break;
		default:
			p++;
		}
	}

	va_end(ap);

	sys_abort();
}

void remap_segments(struct seg_info *seg, int num)
{
	int hpage_size = gethugepagesize();
	int i;
	void *p;

	/* This is the hairy bit, between unmap and remap we enter a
	 * black hole.  We can't call anything which uses static data
	 * (ie. essentially any library function...)
	 */
	for (i = 0; i < num; i++)
		munmap(seg[i].vaddr, seg[i].memsz);

	/* Step 3.  Rebuild the address space with hugetlb mappings */
	/* NB: we can't do the remap as hugepages within the main loop
	 * because of PowerPC: we may need to unmap all the normal
	 * segments before the MMU segment is ok for hugepages */
	for (i = 0; i < num; i++) {
		unsigned long mapsize = ALIGN(seg[i].memsz, hpage_size);

		p = mmap(seg[i].vaddr, mapsize, seg[i].prot,
			 MAP_PRIVATE|MAP_FIXED, seg[i].fd, 0);
		if (p == MAP_FAILED)
			unmapped_abort("Failed to map hugepage segment %u: "
				       "%p-%p (errno=%u)\n", i, seg[i].vaddr,
				       seg[i].vaddr+mapsize, errno);
		if (p != seg[i].vaddr)
			unmapped_abort("Mapped hugepage segment %u (%p-%p) at "
				       "wrong address %p\n", i, seg[i].vaddr,
				       seg[i].vaddr+mapsize, p);
	}
	/* The segments are all back at this point.
	 * and it should be safe to reference static data
	 */
}


static void __attribute__ ((constructor)) setup_elflink(void)
{
	extern Elf_Ehdr __executable_start __attribute__((weak));
	Elf_Ehdr *ehdr = &__executable_start;

	if (! ehdr) {
		DEBUG("Couldn't locate __executable_start, "
		      "not attempting to remap segments\n");
		return;
	}

	parse_phdrs(ehdr);

	if (htlb_num_segs == 0) {
		DEBUG("Executable is not linked for hugepage segments\n");
		return;
	}


	/* Step 1.  Map the hugetlbfs files anywhere to copy data */
	if (prepare_segments(htlb_seg_table, htlb_num_segs) != 0)
		return;

	/* Step 2.  Unmap the old segments, map in the new ones */
	remap_segments(htlb_seg_table, htlb_num_segs);
}

