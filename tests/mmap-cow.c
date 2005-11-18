#include <sys/types.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "hugetests.h"

extern int errno;

#define P "mmap-cow"
#define DESC \
	"* Tests copy-on-write semantics of large pages where a number     *\n"\
	"* of threads map the same file with the MAP_PRIVATE flag.  The    *\n"\
	"* threads then write into their copy of the mapping and recheck   *\n"\
	"* the contents to ensure they were not corrupted by the other     *\n"\
	"* threads.                                                        *"\

#define HTLB_FILE "mmap-cow"
#define BUF_SZ 256
#define MAX_PROCS 100
#define PRIVATE 0
#define SHARED 1

/* Setup Configuration */
int nr_hugepages;		/* Number of huge pages to allocate */
unsigned int threads;		/* Number of threads to run */
char mountpoint[BUF_SZ];	/* Location of mounted hugetlbfs */
char hugetlb_file[BUF_SZ];	/* Name of the hugetlb file we are mapping */

void setup() {
	if (alloc_hugepages(nr_hugepages)) {
		ERROR("Couldn't allocate enough huge pages\n");
		CONFIG();
	}
	if (find_mount_hugetlbfs(mountpoint, BUF_SZ)) {
		ERROR("Couldn't determine hugetlbfs mount point\n");
		CONFIG();
	}
	snprintf(hugetlb_file, BUF_SZ, "%s/%s", mountpoint, HTLB_FILE);
}

void cleanup() {
	unlink(hugetlb_file);
}

int mmap_file(char *mount, char **addr, int *fh, size_t size, int shared)
{
	char fname[BUF_SZ];
	int flags = 0;

	/* First, open the file */
	*fh = open(hugetlb_file, O_CREAT|O_RDWR, 0755);
	if (*fh < 0) {
		PERROR("Unable to open temp file in hugetlbfs");
		CONFIG();
	}

	(shared) ? (flags |= MAP_SHARED) : (flags |= MAP_PRIVATE);
	*addr = mmap(NULL, size, PROT_READ|PROT_WRITE, flags, *fh, 0);
	if (errno != 0) {
		PERROR("Failed to mmap the hugetlb file");
		CONFIG();
	}
	return 0;
}

int do_work(int thread, size_t size) {
	char *addr;
	int fh;
	size_t i;
	char pattern = thread+65;

	if (mmap_file(mountpoint, &addr, &fh, size, PRIVATE)) {
		ERROR("mmap failed\n");
		FAIL();
	}
	Dprintf("%i: Mapped at address %p\n", thread, addr);

	/* Write to the mapping with a distinct pattern */
	Dprintf("%i: Writing %c to the mapping\n", thread, pattern);
	for (i = 0; i < size; i++) {
		memcpy((char *)addr+i, &pattern, 1);
	}

	msync(addr, size, MS_SYNC);
	if (errno != 0) {
		PERROR("msync");
		FAIL();
	}

	/* Verify the pattern */
	for (i = 0; i < size; i++) {
		if (addr[i] != pattern) {
			ERROR("%i: Memory corruption at %p: Got %c, Expected %c\n",
				thread, &addr[i], addr[i], pattern);
			return -1;
		}
	}
	Dprintf("%i: Pattern verified\n", thread);

	/* Munmap the area */
	munmap(addr, size);
	close(fh);
	return 0;
}	

int main(int argc, char ** argv)
{
	char *addr;
	size_t hpage_size, size;
	int i, pid, status, fh;
	int wait_list[MAX_PROCS];

	INIT();

	if (argc < 3) {
		ERROR("Usage: mmap-cow <# threads> <# pages>\n");
		exit(RC_CONFIG);
	}
	nr_hugepages = atoi(argv[2]);
	threads = atoi(argv[1]);
	setup();

	hpage_size = get_hugepage_size();
	size = (nr_hugepages/threads) * hpage_size * 1024;
	Dprintf("hpage_size is %lx, Size is %lu, threads: %lu\n",
		 hpage_size, size, threads);

	/* First, mmap the file with MAP_SHARED and fill with data
	 * If this is not done, then the fault handler will not be
	 * called in the kernel since private mappings will be 
	 * created for the children at prefault time.
	 */
	if (mmap_file(mountpoint, &addr, &fh, size, SHARED)) {
		PERROR("Failed to create shared mapping");
		CONFIG();
	}
	for (i = 0; i < size; i += 8) {
		memcpy(addr+i, "deadbeef", 8);
	}
	
	for (i=0; i<threads; i++) {
		if ((pid = fork()) < 0) {
			PERROR("fork");
			FAIL();
		}
		if (pid == 0) {
			if (do_work(i, size))
				FAIL();
			exit(RC_PASS);
		}
		wait_list[i] = pid;
	}	
	for (i=0; i<threads; i++) {
		waitpid(wait_list[i], &status, 0);
	}

	munmap(addr, size);
	close(fh);

	PASS();
}

