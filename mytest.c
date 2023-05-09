#include "types.h"
#include "user.h"
#include "stat.h"
#include "param.h"

int main()
{	
	char *memory_area = 0;
	// number of free space
	printf(1, "freemem: %d\n", freemem());
	// open the README
	int fd = open("README", 0);

	// private file mapping with MAP_POPULATE
	printf(1, "=================Private file mapping with MAP_POPULATE===================\n");
	printf(1, "freemem: %d\n", freemem());				// number of free space
	memory_area = (char *) mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0);
	printf(1, "mmap result:\n%s\n", memory_area);
	printf(1, "freemem: %d\n", freemem());
	printf(1, "munmap result: %d\n", munmap(0));

	// private anonymous file mapping with MAP_POPULATE
	printf(1, "============Private anonymous file mapping with MAP_POPULATE==============\n");
	printf(1, "freemem: %d\n", freemem());				// number of free space
	printf(1, "mmap result: %x\n", mmap(0, 8192, PROT_READ, MAP_POPULATE|MAP_ANONYMOUS, -1, 0));
	printf(1, "freemem: %d\n", freemem());
	printf(1, "munmap result: %d\n", munmap(0));

	// private file mapping without MAP_POPULATE
	printf(1, "===============Private file mapping without MAP_POPULATE==================\n");
	printf(1, "freemem: %d\n", freemem());				// number of free space
	memory_area = (char *) mmap(0, 8192, PROT_READ, 0, fd, 4096);
	printf(1, "mmap result: %s\n", memory_area);	// print memory mapped file
	printf(1, "freemem: %d\n", freemem());				// number of free space
	printf(1, "munmap result: %d\n", munmap(0));		
	printf(1, "freemem: %d\n", freemem());

	// memory overlapped situation
	printf(1, "============================memory overlapped=============================\n");
	printf(1, "freemem: %d\n", freemem());				// number of free space
	printf(1, "mmap result: %x\n", mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0));
	printf(1, "mmap result: %x\n", mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0));
	printf(1, "freemem: %d\n", freemem());
	printf(1, "munmap result: %d\n", munmap(0));
	printf(1, "munmap result: %d\n", munmap(0));
	printf(1, "freemem: %d\n", freemem());


	exit();
}
