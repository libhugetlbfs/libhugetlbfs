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
#include <link.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/file.h>
#include <linux/unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <elf.h>
#include <dlfcn.h>

#include "version.h"
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
static void write_err(const char *start, int len)
{
	direct_syscall(__NR_write, 2 /*stderr*/, start, len);
}
static void sys_abort(void)
{
	pid_t pid = direct_syscall(__NR_getpid);

	direct_syscall(__NR_kill, pid, SIGABRT);
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

static char share_path[PATH_MAX+1];

#define MAX_HTLB_SEGS	2

struct seg_info {
	void *vaddr;
	unsigned long filesz, memsz, extrasz;
	int prot;
	int fd;
	int index;
};

static struct seg_info htlb_seg_table[MAX_HTLB_SEGS];
static int htlb_num_segs;
static int minimal_copy = 1;
static int sharing; /* =0 */
static unsigned long force_remap; /* =0 */
static long hpage_size;

/**
 * assemble_path - handy wrapper around snprintf() for building paths
 * @dst: buffer of size PATH_MAX+1 to assemble string into
 * @fmt: format string for path
 * @...: printf() style parameters for path
 *
 * assemble_path() builds a path in the target buffer (which must have
 * PATH_MAX+1 available bytes), similar to sprintf().  However, f the
 * assembled path would exceed PATH_MAX characters in length,
 * assemble_path() prints an error and abort()s, so there is no need
 * to check the return value and backout.
 */
static void assemble_path(char *dst, const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(dst, PATH_MAX+1, fmt, ap);
	va_end(ap);

	if (len < 0) {
		ERROR("vsnprintf() error\n");
		abort();
	}

	if (len > PATH_MAX) {
		ERROR("Overflow assembling path\n");
		abort();
	}
}

static void check_memsz()
{
	int i;
	unsigned long memsz_total = 0, memsz_max = 0;
	if (htlb_num_segs == 0)
		return;
	/*
	 * rough heuristic to see if we'll run out of address
	 * space
	 */
	for (i = 0; i < htlb_num_segs; i++) {
		memsz_total += htlb_seg_table[i].memsz;
		if (htlb_seg_table[i].memsz > memsz_max)
			memsz_max = htlb_seg_table[i].memsz;
	}
	/* avoid overflow checking by using two checks */
	DEBUG("Total memsz = %#0lx, memsz of largest segment = %#0lx\n",
			memsz_total, memsz_max);
}

/**
 * find_or_create_share_path - obtain a directory to store the shared
 * hugetlbfs files
 *
 * Checks environment and filesystem to locate a suitable directory
 * for shared hugetlbfs files, creating a new directory if necessary.
 * The determined path is stored in global variable share_path.
 *
 * returns:
 *  -1, on error
 *  0, on success
 */
static int find_or_create_share_path(void)
{
	char *env;
	struct stat sb;
	int ret;

	env = getenv("HUGETLB_SHARE_PATH");
	if (env) {
		/* Given an explicit path */
		if (hugetlbfs_test_path(env) != 0) {
			ERROR("HUGETLB_SHARE_PATH %s is not on a hugetlbfs"
			      " filesystem\n", share_path);
			return -1;
		}
		assemble_path(share_path, "%s", env);
		return 0;
	}

	assemble_path(share_path, "%s/elflink-uid-%d",
		      hugetlbfs_find_path(), getuid());

	ret = mkdir(share_path, 0700);
	if ((ret != 0) && (errno != EEXIST)) {
		ERROR("Error creating share directory %s\n", share_path);
		return -1;
	}

	/* Check the share directory is sane */
	ret = lstat(share_path, &sb);
	if (ret != 0) {
		ERROR("Couldn't stat() %s: %s\n", share_path, strerror(errno));
		return -1;
	}

	if (! S_ISDIR(sb.st_mode)) {
		ERROR("%s is not a directory\n", share_path);
		return -1;
	}

	if (sb.st_uid != getuid()) {
		ERROR("%s has wrong owner (uid=%d instead of %d)\n",
		      share_path, sb.st_uid, getuid());
		return -1;
	}

	if (sb.st_mode & (S_IWGRP | S_IWOTH)) {
		ERROR("%s has bad permissions 0%03o\n",
		      share_path, sb.st_mode);
		return -1;
	}

	return 0;
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

/**
 * get_shared_file_name - create a shared file name from program name,
 * segment number and current word size
 * @htlb_seg_info: pointer to program's segment data
 * @file_path: pointer to a PATH_MAX+1 array to store filename in
 *
 * The file name created is *not* intended to be unique, except when
 * the name, gid or phdr number differ. The goal here is to have a
 * standard means of accessing particular segments of particular
 * executables.
 *
 * returns:
 *   -1, on failure
 *   0, on success
 */
static int get_shared_file_name(struct seg_info *htlb_seg_info, char *file_path)
{
	int ret;
	char binary[PATH_MAX+1];
	char *binary2;

	memset(binary, 0, sizeof(binary));
	ret = readlink("/proc/self/exe", binary, PATH_MAX);
	if (ret < 0) {
		ERROR("shared_file: readlink() on /proc/self/exe "
		      "failed: %s\n", strerror(errno));
		return -1;
	}

	binary2 = basename(binary);
	if (!binary2) {
		ERROR("shared_file: basename() on %s failed: %s\n",
		      binary, strerror(errno));
		return -1;
	}

	assemble_path(file_path, "%s/%s_%zd_%d", share_path, binary2,
		      sizeof(unsigned long) * 8, htlb_seg_info->index);

	return 0;
}

/* Find the .dynamic program header */
static int find_dynamic(Elf_Dyn **dyntab, const Elf_Phdr *phdr, int phnum)
{
	int i = 1;

	while ((phdr[i].p_type != PT_DYNAMIC) && (i < phnum)) {
		++i;
	}
	if (phdr[i].p_type == PT_DYNAMIC) {
		*dyntab = (Elf_Dyn *)phdr[i].p_vaddr;
		return 0;
	} else {
		DEBUG("No dynamic segment found\n");
		return -1;
	}
}

/* Find the dynamic string and symbol tables */
static int find_tables(Elf_Dyn *dyntab, Elf_Sym **symtab, char **strtab)
{
	int i = 1;
	while ((dyntab[i].d_tag != DT_NULL)) {
		if (dyntab[i].d_tag == DT_SYMTAB)
			*symtab = (Elf_Sym *)dyntab[i].d_un.d_ptr;
		else if (dyntab[i].d_tag == DT_STRTAB)
			*strtab = (char *)dyntab[i].d_un.d_ptr;
		i++;
	}

	if (!*symtab) {
		DEBUG("No symbol table found\n");
		return -1;
	}
	if (!*strtab) {
		DEBUG("No string table found\n");
		return -1;
	}
	return 0;
}

/* Find the number of symbol table entries */
static int find_numsyms(Elf_Sym *symtab, char *strtab)
{
	/*
	 * WARNING - The symbol table size calculation does not follow the ELF
	 *           standard, but rather exploits an assumption we enforce in
	 *           our linker scripts that the string table follows
	 *           immediately after the symbol table. The linker scripts
	 *           must maintain this assumption or this code will break.
	 */
	if ((void *)strtab <= (void *)symtab) {
		DEBUG("Could not calculate dynamic symbol table size\n");
		return -1;
	}
	return ((void *)strtab - (void *)symtab) / sizeof(Elf_Sym);
}

/*
 * To reduce the size of the extra copy window, we can eliminate certain
 * symbols based on information in the dynamic section. The following
 * characteristics apply to symbols which may require copying:
 * - Within the BSS
 * - Global or Weak binding
 * - Object type (variable)
 * - Non-zero size (zero size means the symbol is just a marker with no data)
 */
static inline int keep_symbol(char *strtab, Elf_Sym *s, void *start, void *end)
{
	if ((void *)s->st_value < start)
		return 0;
	if ((void *)s->st_value > end)
		return 0;
	if ((ELF_ST_BIND(s->st_info) != STB_GLOBAL) &&
	    (ELF_ST_BIND(s->st_info) != STB_WEAK))
		return 0;
	if (ELF_ST_TYPE(s->st_info) != STT_OBJECT)
		return 0;
	if (s->st_size == 0)
		return 0;

	if (__hugetlbfs_debug)
		DEBUG("symbol to copy at %p: %s\n", (void *)s->st_value,
						strtab + s->st_name);

	return 1;
}

/* If unspecified by the architecture, no extra copying of the plt is needed */
ElfW(Word) __attribute__ ((weak)) plt_extrasz(ElfW(Dyn) *dyntab)
{
	return 0;
}

/*
 * Subtle:  Since libhugetlbfs depends on glibc, we allow it
 * it to be loaded before us.  As part of its init functions, it
 * initializes stdin, stdout, and stderr in the bss.  We need to
 * include these initialized variables in our copy.
 */

static void get_extracopy(struct seg_info *seg, const Elf_Phdr *phdr, int phnum)
{
	Elf_Dyn *dyntab;        /* dynamic segment table */
	Elf_Sym *symtab = NULL; /* dynamic symbol table */
	Elf_Sym *sym;           /* a symbol */
	char *strtab = NULL;    /* string table for dynamic symbols */
	int ret, numsyms, found_sym = 0;
	void *start, *end, *end_orig;
	void *sym_start, *sym_end;
	void *plt_end;

	end_orig = seg->vaddr + seg->memsz;
	start = seg->vaddr + seg->filesz;
	if (seg->filesz == seg->memsz)
		return;
	if (!minimal_copy)
		goto bail2;

	/* Find dynamic program header */
	ret = find_dynamic(&dyntab, phdr, phnum);
	if (ret < 0)
		goto bail;

	/* Find symbol and string tables */
	ret = find_tables(dyntab, &symtab, &strtab);
	if (ret < 0)
		goto bail;

	numsyms = find_numsyms(symtab, strtab);
	if (numsyms < 0)
		goto bail;

	/* 
	 * We must ensure any returns done hereafter have sane start and end 
	 * values, as the criss-cross apple sauce algorithm is beginning 
	 */
	end = start;

	for (sym = symtab; sym < symtab + numsyms; sym++) {
		if (!keep_symbol(strtab, sym, start, end_orig))
			continue;

		/* These are the droids we are looking for */
		found_sym = 1;
		sym_start = (void *)sym->st_value;
		sym_end = (void *)(sym->st_value + sym->st_size);
		if (sym_end > end)
			end = sym_end;
	}

	/*
	 * Some platforms (PowerPC 64bit ELF) place their PLT beyond the filesz
	 * part of the data segment.  When this is the case, we must extend the
	 * copy window to include this data which has been initialized by the
	 * run-time linker.
	 */
	plt_end = start + plt_extrasz(dyntab);
	if (plt_end > end) {
		end = plt_end;
		found_sym = 1;
	}

	if (__hugetlbfs_debug)
		check_bss(end, end_orig);

	if (found_sym) {
		seg->extrasz = end - start;
	}
	/*
	 * else no need to copy anything, so leave seg->extrasz as zero
	 */
	return;

bail:
	DEBUG("Unable to perform minimal copy\n");
bail2:
	seg->extrasz = end_orig - start;
}

#if defined(__powerpc64__) || defined (__powerpc__)
#define SLICE_LOW_TOP		(0x100000000UL)
#define SLICE_LOW_SIZE		(1UL << SLICE_LOW_SHIFT)
#define SLICE_HIGH_SIZE		(1UL << SLICE_HIGH_SHIFT)
#endif

/*
 * Return the address of the start and end of the hugetlb slice
 * containing @addr. A slice is a range of addresses, start inclusive
 * and end exclusive.
 * Note, that since relinking is not supported on ia64, we can leave it
 * out here.
 */
static unsigned long hugetlb_slice_start(unsigned long addr)
{
#if defined(__powerpc64__)
	if (addr < SLICE_LOW_TOP)
		return ALIGN_DOWN(addr, SLICE_LOW_SIZE);
	else if (addr < SLICE_HIGH_SIZE)
		return SLICE_LOW_TOP;
	else
		return ALIGN_DOWN(addr, SLICE_HIGH_SIZE);
#elif defined(__powerpc__)
	return ALIGN_DOWN(addr, SLICE_LOW_SIZE);
#else
	return ALIGN_DOWN(addr, gethugepagesize());
#endif
}

static unsigned long hugetlb_slice_end(unsigned long addr)
{
#if defined(__powerpc64__)
	if (addr < SLICE_LOW_TOP)
		return ALIGN_UP(addr, SLICE_LOW_SIZE) - 1;
	else
		return ALIGN_UP(addr, SLICE_HIGH_SIZE) - 1;
#elif defined(__powerpc__)
	return ALIGN_UP(addr, SLICE_LOW_SIZE) - 1;
#else
	return ALIGN_UP(addr, gethugepagesize()) - 1;
#endif
}

static unsigned long hugetlb_next_slice_start(unsigned long addr)
{
	return hugetlb_slice_end(addr) + 1;
}

static unsigned long hugetlb_prev_slice_end(unsigned long addr)
{
	return hugetlb_slice_start(addr) - 1;
}

/*
 * Store a copy of the given program header 
 */
int save_phdr(int table_idx, int phnum, const ElfW(Phdr) *phdr)
{
	int prot = 0;

	if (table_idx >= MAX_HTLB_SEGS) {
		ERROR("Executable has too many segments (max %d)\n",
			MAX_HTLB_SEGS);
		htlb_num_segs = 0;
		return -1;
	}

	if (phdr->p_flags & PF_R)
		prot |= PROT_READ;
	if (phdr->p_flags & PF_W)
		prot |= PROT_WRITE;
	if (phdr->p_flags & PF_X)
		prot |= PROT_EXEC;
	
	htlb_seg_table[table_idx].vaddr = (void *) phdr->p_vaddr;
	htlb_seg_table[table_idx].filesz = phdr->p_filesz;
	htlb_seg_table[table_idx].memsz = phdr->p_memsz;
	htlb_seg_table[table_idx].prot = prot;
	htlb_seg_table[table_idx].index = phnum;

	DEBUG("Segment %d (phdr %d): %#0lx-%#0lx  (filesz=%#0lx) "
		"(prot = %#0x)\n", table_idx, phnum,
		(unsigned long)  phdr->p_vaddr,
		(unsigned long) phdr->p_vaddr + phdr->p_memsz,
		(unsigned long) phdr->p_filesz, (unsigned int) prot);

	return 0;
}

/*
 * Parse the phdrs of a program linked with the libhugetlbfs linker scripts
 */
static
int parse_elf_relinked(struct dl_phdr_info *info, size_t size, void *data)
{
	int i;

	for (i = 0; i < info->dlpi_phnum; i++) {
		if (info->dlpi_phdr[i].p_type != PT_LOAD)
			continue;

		if (!(info->dlpi_phdr[i].p_flags & PF_LINUX_HUGETLB))
			continue;

		if (save_phdr(htlb_num_segs, i, &info->dlpi_phdr[i]))
			return 1;

		get_extracopy(&htlb_seg_table[htlb_num_segs],
				&info->dlpi_phdr[0], info->dlpi_phnum);

		htlb_num_segs++;
	}
	if (__hugetlbfs_debug)
		check_memsz();
	return 1;
}

/*
 * Parse the phdrs of a normal program to attempt partial segment remapping
 */
static
int parse_elf_partial(struct dl_phdr_info *info, size_t size, void *data)
{
	unsigned long vaddr, memsz, gap;
	unsigned long slice_end;
	int i;

	/* This should never actually be called more than once in an
	 * iteration: we assume that dl_iterate_phdrs() always gives
	 * us the main program's phdrs on the first iteration, and
	 * always return 1 to cease iteration at that point. */

	for (i = 0; i < info->dlpi_phnum; i++) {
		if (info->dlpi_phdr[i].p_type != PT_LOAD)
			continue;

		/*
		 * Partial segment remapping only makes sense if the
		 * memory size of the segment is larger than the
		 * granularity at which hugepages can be used. This
		 * mostly affects ppc, where the segment must be larger
		 * than 256M. This guarantees that remapping the binary
		 * in this forced way won't violate any contiguity
		 * constraints.
		 */
		vaddr = hugetlb_next_slice_start(info->dlpi_phdr[i].p_vaddr);
		gap = vaddr - info->dlpi_phdr[i].p_vaddr;
		slice_end = hugetlb_slice_end(vaddr);
		/*
		 * we should stop remapping just before the slice
		 * containing the end of the memsz portion (taking away
		 * the gap of the memsz)
		 */
		memsz = info->dlpi_phdr[i].p_memsz;
		if (memsz < gap) {
			DEBUG("Segment %d's unaligned memsz is too small: "
					"%#0lx < %#0lx\n",
					i, memsz, gap);
			continue;
		}
		memsz -= gap;
		if (memsz < (slice_end - vaddr)) {
			DEBUG("Segment %d's aligned memsz is too small: "
					"%#0lx < %#0lx\n",
					i, memsz, slice_end - vaddr);
			continue;
		}
		memsz = hugetlb_prev_slice_end(vaddr + memsz) - vaddr;

		if (save_phdr(htlb_num_segs, i, &info->dlpi_phdr[i]))
			return 1;

		/*
		 * When remapping partial segments, we create a sub-segment
		 * that is based on the original.  For this reason, we must
		 * make some changes to the phdr captured by save_phdr():
		 * 	vaddr is aligned upwards to a slice boundary
		 * 	memsz is aligned downwards to a slice boundary
		 * 	filesz is set to memsz to force all memory to be copied
		 */
		htlb_seg_table[htlb_num_segs].vaddr = (void *)vaddr;
		htlb_seg_table[htlb_num_segs].filesz = memsz;
		htlb_seg_table[htlb_num_segs].memsz = memsz;

		htlb_num_segs++;
	}
	return 1;
}

/*
 * Copy a program segment into a huge page. If possible, try to copy the
 * smallest amount of data possible, unless the user disables this 
 * optimization via the HUGETLB_ELFMAP environment variable.
 */
static int prepare_segment(struct seg_info *seg)
{
	void *p;
	unsigned long size;

	size = ALIGN(seg->filesz + seg->extrasz, hpage_size);

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

	DEBUG("Mapped hugeseg at %p. Copying %#0lx bytes and %#0lx extra bytes"
		" from %p...", p, seg->filesz, seg->extrasz, seg->vaddr);
	memcpy(p, seg->vaddr, seg->filesz + seg->extrasz);
	DEBUG_CONT("done\n");

	munmap(p, size);

	return 0;
}

/**
 * find_or_prepare_shared_file - get one shareable file
 * @htlb_seg_info: pointer to program's segment data
 *
 * This function either locates a hugetlbfs file already containing
 * data for a given program segment, or creates one if it doesn't
 * already exist.
 *
 * We use the following algorithm to ensure that when processes race
 * to instantiate the hugepage file, we will never obtain an
 * incompletely prepared file or have multiple processes prepar
 * separate copies of the file.
 *	- first open 'filename.tmp' with O_EXCL (this acts as a lockfile)
 *	- second open 'filename' with O_RDONLY (even if the first open
 *	  succeeded).
 * Then:
 * 	- If both opens succeed, close the O_EXCL open, unlink
 * filename.tmp and use the O_RDONLY fd.  (Somebody else has prepared
 * the file already)
 * 	- If only the O_RDONLY open suceeds, and the O_EXCL open
 * fails with EEXIST, just used the O_RDONLY fd. (Somebody else has
 * prepared the file already, but we raced with their rename()).
 * 	- If only the O_EXCL open suceeds, and the O_RDONLY fails with
 * ENOENT, prepare the the O_EXCL open, then rename() filename.tmp to
 * filename. (We're the first in, we have to prepare the file).
 * 	- If both opens fail, with EEXIST and ENOENT, respectively,
 * wait for a little while, then try again from the beginning
 * (Somebody else is preparing the file, but hasn't finished yet)
 *
 * returns:
 *   -1, on failure
 *   0, on success
 */
static int find_or_prepare_shared_file(struct seg_info *htlb_seg_info)
{
	int fdx, fds;
	int errnox, errnos;
	int ret;
	char final_path[PATH_MAX+1];
	char tmp_path[PATH_MAX+1];

	ret = get_shared_file_name(htlb_seg_info, final_path);
	if (ret < 0)
		return -1;
	assemble_path(tmp_path, "%s.tmp", final_path);

	do {
		/* NB: mode is modified by umask */
		fdx = open(tmp_path, O_CREAT | O_EXCL | O_RDWR, 0666);
		errnox = errno;
		fds = open(final_path, O_RDONLY);
		errnos = errno;

		if (fds >= 0) {
			/* Got an already-prepared file -> use it */
			if (fdx > 0) {
				/* Also got an exclusive file -> clean up */
				ret = unlink(tmp_path);
				if (ret != 0)
					ERROR("shared_file: unable to clean up"
					      " unneeded file %s: %s\n",
					      tmp_path, strerror(errno));
				close(fdx);
			} else if (errnox != EEXIST) {
				WARNING("shared_file: Unexpected failure on exclusive"
					" open of %s: %s\n", tmp_path,
					strerror(errnox));
			}
			htlb_seg_info->fd = fds;
			return 0;
		}

		if (fdx >= 0) {
			/* It's our job to prepare */
			if (errnos != ENOENT)
				WARNING("shared_file: Unexpected failure on"
					" shared open of %s: %s\n", final_path,
					strerror(errnos));

			htlb_seg_info->fd = fdx;

			DEBUG("Got unpopulated shared fd -- Preparing\n");
			ret = prepare_segment(htlb_seg_info);
			if (ret < 0)
				goto fail;

			DEBUG("Prepare succeeded\n");
			/* move to permanent location */
			ret = rename(tmp_path, final_path);
			if (ret != 0) {
				ERROR("shared_file: unable to rename %s"
				      " to %s: %s\n", tmp_path, final_path,
				      strerror(errno));
				goto fail;
			}

			return 0;
		}

		/* Both opens failed, somebody else is still preparing */
		/* Wait and try again */
		sleep(1);
		/* FIXME: should have a timeout */
	} while (1);

 fail:
	if (fdx > 0) {
		ret = unlink(tmp_path);
		if (ret != 0)
			ERROR("shared_file: Unable to clean up temp file %s on"
			      " failure: %s\n", tmp_path, strerror(errno));
		close(fdx);
	}

	return -1;
}

/**
 * obtain_prepared_file - multiplex callers depending on if
 * sharing or not
 * @htlb_seg_info: pointer to program's segment data
 *
 * returns:
 *  -1, on error
 *  0, on success
 */
static int obtain_prepared_file(struct seg_info *htlb_seg_info)
{
	int fd = -1;
	int ret, pid, status;

	/* Share only read-only segments */
	if (sharing && !(htlb_seg_info->prot & PROT_WRITE)) {
		/* first, try to share */
		ret = find_or_prepare_shared_file(htlb_seg_info);
		if (ret == 0)
			return 0;
		/* but, fall through to unlinked files, if sharing fails */
		DEBUG("Falling back to unlinked files\n");
	}
	fd = hugetlbfs_unlinked_fd();
	if (fd < 0)
		return -1;
	htlb_seg_info->fd = fd;

	/* [PPC] Prior to 2.6.22 (which added slices), our temporary hugepage
	 * mappings are placed in the segment before the stack. This 'taints' that
	 * segment for be hugepage-only for the lifetime of the process, resulting
	 * in a maximum stack size of 256MB. If we instead create our hugepage
	 * mappings in a child process, we can avoid this problem.
	 *
	 * This does not adversely affect non-PPC platforms so do it everywhere.
	 */
	if ((pid = fork()) < 0) {
		DEBUG("fork failed");
		return -1;
	}
	if (pid == 0) {
		ret = prepare_segment(htlb_seg_info);
		if (ret < 0) {
			DEBUG("Failed to prepare segment\n");
			exit(1);
		}
		else
			exit(0);
	}
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		DEBUG("waitpid failed");
		return -1;
	}

	DEBUG("Prepare succeeded\n");
	return 0;
}

static void remap_segments(struct seg_info *seg, int num)
{
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

static int check_env(void)
{
	char *env, *env2;
	extern Elf_Ehdr __executable_start __attribute__((weak));

	env = getenv("HUGETLB_ELFMAP");
	if (env && (strcasecmp(env, "no") == 0)) {
		DEBUG("HUGETLB_ELFMAP=%s, not attempting to remap program "
		      "segments\n", env);
		return -1;
	}

	env = getenv("LD_PRELOAD");
	if (env && strstr(env, "libhugetlbfs")) {
		env2 = getenv("HUGETLB_FORCE_ELFMAP");
		if (env2 && (strcasecmp(env2, "yes") == 0)) {
			force_remap = 1;
			DEBUG("HUGETLB_FORCE_ELFMAP=%s, "
					"enabling partial segment "
					"remapping for non-relinked "
					"binaries\n",
					env2);
			DEBUG("Disabling filesz copy optimization\n");
			minimal_copy = 0;
		} else {
			if (&__executable_start) {
				ERROR("LD_PRELOAD is incompatible with segment remapping\n");
				ERROR("Segment remapping has been DISABLED\n");
				return -1;
			}
		}
	}

	env = getenv("HUGETLB_MINIMAL_COPY");
	if (minimal_copy && env && (strcasecmp(env, "no") == 0)) {
		DEBUG("HUGETLB_MINIMAL_COPY=%s, disabling filesz copy "
			"optimization\n", env);
		minimal_copy = 0;
	}

	env = getenv("HUGETLB_SHARE");
	if (env)
		sharing = atoi(env);
	if (sharing == 2) {
		ERROR("HUGETLB_SHARE=%d, however sharing of writable\n"
			"segments has been deprecated and is now disabled\n",
			sharing);
		sharing = 0;
	} else {
		DEBUG("HUGETLB_SHARE=%d, sharing ", sharing);
		if (sharing == 1) {
			DEBUG_CONT("enabled for only read-only segments\n");
		} else {
			DEBUG_CONT("disabled\n");
			sharing = 0;
		}
	}

	return 0;
}

/*
 * Parse an ELF header and record segment information for any segments
 * which contain hugetlb information.
 */
static int parse_elf()
{
	extern Elf_Ehdr __executable_start __attribute__((weak));

	/* a normal, not relinked binary */
	if (! (&__executable_start)) {
		if (force_remap) {
			dl_iterate_phdr(parse_elf_partial, NULL);
			if (htlb_num_segs == 0) {
				DEBUG("No segments were appropriate for "
						"partial remapping\n");
				return -1;
			}
		} else {
			DEBUG("Couldn't locate __executable_start, "
				"not attempting to remap segments\n");
			return -1;
		}
	} else {
		dl_iterate_phdr(parse_elf_relinked, NULL);
		if (htlb_num_segs == 0) {
			DEBUG("No segments were appropriate for "
					"remapping\n");
			return -1;
		}
	}

	return 0;
}

void __hugetlbfs_setup_elflink(void)
{
	int i, ret;

	if (check_env())
		return;

	if (parse_elf())
		return;

	hpage_size = gethugepagesize();
	if (hpage_size <= 0) {
		if (errno == ENOSYS)
			ERROR("Hugepages unavailable\n");
		else if (errno == EOVERFLOW)
			ERROR("Hugepage size too large\n");
		else
			ERROR("Hugepage size (%s)\n", strerror(errno));
		return;
	}

	DEBUG("libhugetlbfs version: %s\n", VERSION);

	/* Do we need to find a share directory */
	if (sharing) {
		ret = find_or_create_share_path();
		if (ret != 0)
			return;
	}

	/* Step 1.  Obtain hugepage files with our program data */
	for (i = 0; i < htlb_num_segs; i++) {
		ret = obtain_prepared_file(&htlb_seg_table[i]);
		if (ret < 0) {
			DEBUG("Failed to setup hugetlbfs file\n");
			return;
		}
	}

	/* Step 3.  Unmap the old segments, map in the new ones */
	remap_segments(htlb_seg_table, htlb_num_segs);
}
