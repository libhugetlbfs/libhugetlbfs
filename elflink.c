/*
 * libhugetlbfs - Easy use of Linux hugepages
 * Copyright (C) 2005-2006 David Gibson & Adam Litke, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define _GNU_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <signal.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <elf.h>
#include <dlfcn.h>

#include "hugetlbfs.h"
#include "libhugetlbfs_internal.h"

#ifdef __LP64__
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Phdr	Elf64_Phdr
#define Elf_Dyn		Elf64_Dyn
#define Elf_Sym		Elf64_Sym
#define ELF_ST_BIND(x)  ELF64_ST_BIND(x)
#define ELF_ST_TYPE(x)  ELF64_ST_TYPE(x)
#else
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Phdr	Elf32_Phdr
#define Elf_Dyn		Elf32_Dyn
#define Elf_Sym		Elf32_Sym
#define ELF_ST_BIND(x)  ELF64_ST_BIND(x)
#define ELF_ST_TYPE(x)  ELF64_ST_TYPE(x)
#endif

#ifdef __syscall_return
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
#else /* __syscall_return */
#warning __syscall_return macro not available. Some debugging will be \
	disabled during executable remapping
static void unmapped_abort(const char *fmt, ...)
{
}
#endif /* __syscall_return */

#define MAX_HTLB_SEGS	2

static struct seg_info htlb_seg_table[MAX_HTLB_SEGS];
static int htlb_num_segs;
static int minimal_copy = 1;
int __debug = 0;
static Elf_Ehdr *ehdr;

/*
 * Parse an ELF header and record segment information for any segments
 * which contain hugetlb information.
 */

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

		DEBUG("Hugepage segment %d "
			"(phdr %d): %#0lx-%#0lx  (filesz=%#0lx) "
			"(prot = %#0x)\n",
			htlb_num_segs, i, vaddr, vaddr+memsz, filesz, prot);

		htlb_seg_table[htlb_num_segs].vaddr = (void *)vaddr;
		htlb_seg_table[htlb_num_segs].filesz = filesz;
		htlb_seg_table[htlb_num_segs].memsz = memsz;
		htlb_seg_table[htlb_num_segs].prot = prot;
		htlb_num_segs++;
	}
}

/* 
 * Look for non-zero BSS data inside a range and print out any matches
 */

static void check_bss(unsigned long *start, unsigned long *end)
{
	unsigned long *addr;

	for (addr = start; addr < end; addr++) {
		if (*addr != 0)
			WARNING("Non-zero BSS data @ %p: %lx\n", addr, *addr);
	}
}

/* 
 * Subtle:  Since libhugetlbfs depends on glibc, we allow it
 * it to be loaded before us.  As part of its init functions, it
 * initializes stdin, stdout, and stderr in the bss.  We need to
 * include these initialized variables in our copy.
 */

static void get_extracopy(struct seg_info *seg, void **extra_start, 
							void **extra_end)
{
	Elf_Dyn *dyntab;        /* dynamic segment table */
	Elf_Phdr *phdr;         /* program header table */
	Elf_Sym *symtab = NULL; /* dynamic symbol table */
	Elf_Sym *sym;           /* a symbol */
	char *strtab = NULL;    /* string table for dynamic symbols */
	int i, found_sym = 0;
	int numsyms;            /* number of symbols in dynamic symbol table */
	void *start, *end, *start_orig, *end_orig;
	void *sym_start, *sym_end;

	end_orig = seg->vaddr + seg->memsz;
	start_orig = seg->vaddr + seg->filesz;
	if (seg->filesz == seg->memsz)
		goto bail2;
	if (!minimal_copy)
		goto bail2;

	/* Find dynamic section */
	i = 1;
	phdr = (Elf_Phdr *)((char *)ehdr + ehdr->e_phoff);
	while ((phdr[i].p_type != PT_DYNAMIC) && (i < ehdr->e_phnum)) {
		++i;
	}
	if (phdr[i].p_type == PT_DYNAMIC) {
		dyntab = (Elf_Dyn *)phdr[i].p_vaddr;
	} else {
		DEBUG("No dynamic segment found\n");
		goto bail;
	}

	/* Find symbol and string tables */
	i = 1;
	while ((dyntab[i].d_tag != DT_NULL)) {
		if (dyntab[i].d_tag == DT_SYMTAB)
			symtab = (Elf_Sym *)dyntab[i].d_un.d_ptr;
		else if (dyntab[i].d_tag == DT_STRTAB)
			strtab = (char *)dyntab[i].d_un.d_ptr;
		i++;
	}
			
	if (!symtab) {
		DEBUG("No symbol table found\n");
		goto bail;
	}
	if (!strtab) {
		DEBUG("No string table found\n");
		goto bail;
	}

	/*
	 * WARNING - The symbol table size calculation does not follow the ELF
	 *           standard, but rather exploits an assumption we enforce in
	 *           our linker scripts that the string table follows
	 *           immediately after the symbol table. The linker scripts
	 *           must maintain this assumption or this code will break.
	 */
	if ((void *)strtab <= (void *)symtab) {
		DEBUG("Could not calculate dynamic symbol table size\n");
		goto bail;
	}
	numsyms = ((void *)strtab - (void *)symtab) / sizeof(Elf_Sym);

	/* 
	 * We must ensure any returns done hereafter have sane start and end 
	 * values, as the criss-cross apple sauce algorithm is beginning 
	 */
	start = end_orig;
	end = start_orig;

	/* 
	 * To reduce the size of the extra copy window, we can eliminate certain
	 * symbols based on information in the dynamic section.  The following
	 * characteristics apply to symbols which may require copying:
	 * - Within the BSS
	 * - Global scope
	 * - Object type (variable)
	 * - Non-zero size (zero size means the symbol is just a marker with no
	 *   data)
	 */
	for (sym = symtab; sym < symtab + numsyms; sym++) {
		if (((void *)sym->st_value < start_orig) || 
			((void *)sym->st_value > end_orig) ||
			(ELF_ST_BIND(sym->st_info) != STB_GLOBAL) ||
			(ELF_ST_TYPE(sym->st_info) != STT_OBJECT) ||
			(sym->st_size == 0))
			continue;
		/* TODO - add filtering so that we only look at symbols from glibc 
		   (@@GLIBC_*) */

		/* These are the droids we are looking for */
		found_sym = 1;
		sym_start = (void *)sym->st_value;
		sym_end = (void *)(sym->st_value + sym->st_size);
		if (sym_start < start)
			start = sym_start;
		if (sym_end > end)
			end = sym_end;
	}

	if (__debug)
		check_bss(end, end_orig);

	if (found_sym) {
		/* Return the copy window */
		*extra_start = start;
		*extra_end = end;
		return;
	} else {
		/* No need to copy anything */
		*extra_start = start_orig;
		*extra_end = start_orig;
		goto bail3;
	}

bail:
	DEBUG("Unable to perform minimal copy\n");
bail2:
	*extra_start = start_orig;
	*extra_end = end_orig;
bail3:
	return;
}

/*
 * Copy a program segment into a huge page. If possible, try to copy the
 * smallest amount of data possible, unless the user disables this 
 * optimization via the HUGETLB_ELFMAP environment variable.
 */

static int prepare_segment(struct seg_info *seg)
{
	int hpage_size = gethugepagesize();
	void *p, *extra_start, *extra_end;
	unsigned long gap;
	unsigned long size;

	/*
	 * Calculate the BSS size that we must copy in order to minimize
	 * the size of the shared mapping.
	 */
	get_extracopy(seg, &extra_start, &extra_end);
	size = ALIGN((unsigned long)extra_end - (unsigned long)seg->vaddr,
				hpage_size);

	/* Prepare the hugetlbfs file */

	p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, seg->fd, 0);
	if (p == MAP_FAILED) {
		ERROR("Couldn't map hugepage segment to copy data: %s\n",
			strerror(errno));
		return -1;
	}

	/* 
	 * Subtle, copying only filesz bytes of the segment
	 * allows for much better performance than copying all of
	 * memsz but it requires that all data (such as the plt)
	 * be contained in the filesz portion of the segment.
	 */

	DEBUG("Mapped hugeseg at %p. Copying %#0lx bytes from %p...\n",
	      p, seg->filesz, seg->vaddr);
	memcpy(p, seg->vaddr, seg->filesz);
	DEBUG_CONT("done\n");

	if (extra_end > extra_start) {
		DEBUG("Copying extra %#0lx bytes from %p...\n", 
			(unsigned long)(extra_end - extra_start), extra_start);
		gap = extra_start - (seg->vaddr + seg->filesz);
		memcpy((p + seg->filesz + gap), extra_start, (extra_end - extra_start));
		DEBUG_CONT("done\n");
	}

	munmap(p, size);

	return 0;
}

static void remap_segments(struct seg_info *seg, int num)
{
	int hpage_size = gethugepagesize();
	int i;
	void *p;

	/*
	 * XXX: The bogus call to mmap below forces ld.so to resolve the
	 * mmap symbol before we unmap the plt in the data segment
	 * below.  This might only be needed in the case where sharing
	 * is enabled and the hugetlbfs files have already been prepared
	 * by another process.
	 */
	 p = mmap(0, 0, 0, 0, 0, 0);

	/* This is the hairy bit, between unmap and remap we enter a
	 * black hole.  We can't call anything which uses static data
	 * (ie. essentially any library function...)
	 */
	for (i = 0; i < num; i++)
		munmap(seg[i].vaddr, seg[i].memsz);

	/* Step 4.  Rebuild the address space with hugetlb mappings */
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

static int maybe_prepare(int fd_state, struct seg_info *seg)
{
	int ret, reply = 0;

	switch (fd_state) {
		case 0:
			DEBUG("Got populated shared fd -- Not preparing\n");
			return 0;

		case 1:
			DEBUG("Got unpopulated shared fd -- Preparing\n");
			reply = 1;
			break;
		case 2:
			DEBUG("Got unshared fd, wanted shared -- Preparing\n");
			break;
		case 3:
			DEBUG("Got unshared fd as expected -- Preparing\n");
			break;

		default:
			ERROR("Unexpected fd state: %d in maybe_prepare\n",
				fd_state);
			return -1;
	}

	ret = prepare_segment(seg);
	if (ret < 0) {
		/* notify daemon of failed prepare */
		DEBUG("Failed to prepare segment\n");
		finished_prepare(seg, -1);
		return -1;
	}
	DEBUG("Prepare succeeded\n");
	if (reply) {
		ret = finished_prepare(seg, 0);
		if (ret < 0)
			DEBUG("Failed to communicate successful "
				"prepare to hugetlbd\n");
	}
	return ret;
}

static int check_env(void)
{
	char *env;

	env = getenv("HUGETLB_ELFMAP");
	if (env && (strcasecmp(env, "no") == 0)) {
		DEBUG("HUGETLB_ELFMAP=%s, not attempting to remap program "
		      "segments\n", env);
		return -1;
	}

	env = getenv("LD_PRELOAD");
	if (env && strstr(env, "libhugetlbfs")) {
		ERROR("LD_PRELOAD is incompatible with segment remapping\n");
		ERROR("Segment remapping has been DISABLED\n");
		return -1;
	}

	env = getenv("HUGETLB_MINIMAL_COPY");
	if (env && (strcasecmp(env, "no") == 0)) {
		DEBUG("HUGETLB_MINIMAL_COPY=%s, disabling filesz copy "
			"optimization\n", env);
		minimal_copy = 0;
	}

	env = getenv("HUGETLB_DEBUG");
	if (env) {
		DEBUG("HUGETLB_DEBUG=%s, enabling extra checking\n", env);
		__debug = 1;
	}

	return 0;
}

static void __attribute__ ((constructor)) setup_elflink(void)
{
	extern Elf_Ehdr __executable_start __attribute__((weak));
	ehdr = &__executable_start;
	int ret, i;

	if (! ehdr) {
		DEBUG("Couldn't locate __executable_start, "
		      "not attempting to remap segments\n");
		return;
	}

	if (check_env())
		return;

	parse_phdrs(ehdr);

	if (htlb_num_segs == 0) {
		DEBUG("Executable is not linked for hugepage segments\n");
		return;
	}

	/* Step 1.  Get access to the files we're going to mmap for the
	 * segments */
	for (i = 0; i < htlb_num_segs; i++) {
		ret = hugetlbfs_set_fd(&htlb_seg_table[i]);
		if (ret < 0) {
			DEBUG("Failed to setup hugetlbfs file\n");
			return;
		}

		/* Step 2.  Map the hugetlbfs files anywhere to copy data, if we
		 * need to prepare at all */
		ret = maybe_prepare(ret, &htlb_seg_table[i]);
		if (ret < 0) {
			DEBUG("Failed to prepare hugetlbfs file\n");
			return;
		}
	}

	/* Step 3.  Unmap the old segments, map in the new ones */
	remap_segments(htlb_seg_table, htlb_num_segs);
}
