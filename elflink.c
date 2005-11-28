#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <elf.h>

#ifdef __LP64__
#define Elf_Ehdr	Elf64_Ehdr
#define Elf_Phdr	Elf64_Phdr
#else
#define Elf_Ehdr	Elf32_Ehdr
#define Elf_Phdr	Elf32_Phdr
#endif

#include "hugetlbfs.h"
#include "libhugetlbfs_internal.h"

struct seg_info {
	void *vaddr;
	unsigned long filesz, memsz;
	int prot;
	int fd;
};

#define MAX_HTLB_SEGS	2

static struct seg_info htlb_seg_table[MAX_HTLB_SEGS];
static int htlb_num_segs;

static void parse_phdrs(Elf_Ehdr *ehdr)
{
	Elf_Phdr *phdr;
	int i;

	DEBUG("parse_phdrs: ELF header at %p\n", ehdr);
	phdr = (Elf_Phdr *)((char *)ehdr + ehdr->e_phoff);
	DEBUG("parse_phdrs: ELF program headers at %p\n", phdr);

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

		DEBUG("Hugepage segment %d (phdr %d): %lx-%lx  (filesz=%lx)\n",
		      htlb_num_segs, i, vaddr, vaddr+memsz, filesz);

		htlb_seg_table[htlb_num_segs].vaddr = (void *)vaddr;
		htlb_seg_table[htlb_num_segs].filesz = filesz;
		htlb_seg_table[htlb_num_segs].memsz = memsz;
		htlb_seg_table[htlb_num_segs].prot = prot;
		htlb_num_segs++;
	}
}

static void remap_segments(struct seg_info *seg, int num)
{
	int hpage_size = gethugepagesize();
	int i;
	void *tmp;
	
	/* Prepare the hugetlbfs files */
	for (i = 0; i < num; i++)
		seg[i].fd = hugetlbfs_unlinked_fd();
	
	/* Step 1.  Map the hugetlbfs files anywhere to copy data */
	/* Step 2.  We can then unmap all the areas */
	/* From here until after step 3 we enter a black hole.
	 * Since we are unmapping program segments, we can't call anything
	 * which uses static data (ie. printf)
	 */
	for (i = 0; i < num; i++) {
		tmp = mmap(NULL, ALIGN(seg[i].memsz, hpage_size),
			   PROT_READ|PROT_WRITE, MAP_SHARED, seg[i].fd, 0);
		if (tmp == MAP_FAILED)
			ERROR("Couldn't map hugepage segment to copy data\n");

		memcpy(tmp, seg[i].vaddr, seg[i].memsz);

		munmap(tmp, seg[i].memsz);
		munmap(seg[i].vaddr, seg[i].memsz);
	}

	/* NB: we can't do the remap as hugepages within the main loop
	 * because of PowerPC: we may nede to unmap all the normal
	 * segments before the MMU segment is ok for hugepages */
	/* Step 3.  Rebuild the address space with hugetlb mappings */
	for (i = 0; i < num; i++) {
		tmp = mmap(seg[i].vaddr, ALIGN(seg[i].memsz, hpage_size),
			   seg[i].prot, MAP_PRIVATE|MAP_FIXED, seg[i].fd, 0);
		if (tmp == MAP_FAILED) {
			ERROR("Failed to map hugepage segment %d (%p-%p): %s\n",
			      i, seg[i].vaddr, seg[i].vaddr+seg[i].memsz,
			      strerror(errno));
			abort();
		}
		if (tmp != seg[i].vaddr) {
			/* FIXME: probaly not safe to call printf here... */
			ERROR("Mapped hugepage segment %d (%p-%p) at wrong address %p\n",
			      i, seg[i].vaddr, seg[i].vaddr+seg[i].memsz, tmp);
			abort();
		}
	}
	/* The segments are all back at this point.
	 * and it should be safe to reference static data
	 */
}

static void __attribute__ ((constructor)) setup_elflink(void)
{
	char *env;
	extern Elf_Ehdr __executable_start __attribute__((weak));
	Elf_Ehdr *ehdr = &__executable_start;

	env = getenv("HUGETLB_ELF");
	if (! env)
		return;

	if (! ehdr) {
		DEBUG("Couldn't locate __executable_start, "
		      "not attempting to remap segments\n");
		return;
	}

	parse_phdrs(ehdr);
	if (htlb_num_segs)
		remap_segments(htlb_seg_table, htlb_num_segs);
}
