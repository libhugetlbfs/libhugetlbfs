#define _GNU_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <elf.h>
#include <dlfcn.h>

#ifdef __LP64__
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Phdr	Elf64_Phdr
#else
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Phdr	Elf32_Phdr
#endif

#include "hugetlbfs.h"
#include "libhugetlbfs_internal.h"

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
		int copysize = seg[i].memsz;
		int size = ALIGN(seg[i].memsz, hpage_size);

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
		
		DEBUG("Mapped hugeseg %d at %p. Copying %d bytes from %p...",
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
 * FIXME: This works in practice, but I suspect it is not guaranteed
 * safe: the library functions we call could in theory call other
 * functions via the PLT which will blow up. */
static void unmapped_abort(const char *fmt, ...)
{
	static int (*p_vfprintf)(FILE *, const char *, va_list);
	static void (*p_abort)(void);
	static FILE *se;
	va_list ap;

	if (!fmt) {
		/* Setup */
		p_vfprintf = dlsym(RTLD_DEFAULT, "vfprintf");
		p_abort = dlsym(RTLD_DEFAULT, "abort");
		se = stderr;
		return;
	}

	va_start(ap, fmt);
	(*p_vfprintf)(se, fmt, ap);
	va_end(ap);

	(*p_abort)();
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

	/* Initialize a function for printing errors and aborting
	 * which is safe in the event of segments being unmapped */
	unmapped_abort(NULL);

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
			unmapped_abort("Failed to map hugepage segment %d: "
				       "%p-%p (errno=%d)\n", i, seg[i].vaddr,
				       seg[i].vaddr+mapsize, errno);
		if (p != seg[i].vaddr)
			unmapped_abort("Mapped hugepage segment %d (%p-%p) at "
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

