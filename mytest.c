#include "types.h"
#include "user.h"
#include "stat.h"
#include "param.h"

int main()
{	
	char *memory_area = 0;
	char *memory_area2 = 0;
	// number of free space
	printf(1, "freemem: %d\n", freemem());
	// open the README
	int fd = open("README", 0);

	// number of free space
	printf(1, "\nfirst free memory number: %d\n", freemem());
	printf(1, "\n=========================Some Error Handling==============================\n");
	printf(1, "The mmap results are like these...\n");
	printf(1, "address is not page-aligned: %d\n", mmap(3, 8192, PROT_READ, MAP_POPULATE, fd, 0));
	printf(1, "length is not page-aligned: %d\n", mmap(0, 235, PROT_READ, MAP_POPULATE, fd, 0));
	printf(1, "offset is negative: %d\n", mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, -3));
	printf(1, "length is 0: %d\n", mmap(0, 0, PROT_READ, MAP_POPULATE, fd, 0));
	printf(1, "prot is invalid: %d\n", mmap(0, 8192, 0, MAP_POPULATE, fd, 0));
	printf(1, "not anonymous but fd is negative: %d\n", mmap(3, 8192, PROT_READ, MAP_POPULATE, -1, 0));

	// private file mapping with MAP_POPULATE
	// Initially, if we use page, allocate memory for PTE in page table pgdir
	// So, free memory number will decrease by 3
	printf(1, "\n=================Private file mapping with MAP_POPULATE===================\n");
	printf(1, "free memory number: %d\n", freemem());				// number of free space
	memory_area = (char *) mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0);
	// README first four characters are 'N', 'O', 'T', and 'E' (NOTE)
	printf(1, "mmap result (first four letters): %c%c%c%c\n", memory_area[0], memory_area[1], memory_area[2], memory_area[3]);
	printf(1, "free memory number after mmap: %d\n", freemem());
	printf(1, "munmap result: %d\n", munmap((uint) memory_area));
	printf(1, "free memory number after munmap: %d\n", freemem());

	// private anonymous file mapping with MAP_POPULATE
	printf(1, "\n============Private anonymous file mapping with MAP_POPULATE==============\n");
	printf(1, "ffree memory number: %d\n", freemem());				// number of free space
	memory_area = (char *) mmap(0, 8192, PROT_READ, MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
	printf(1, "mmap result: %x\n", (uint) memory_area);
	printf(1, "free memory number: %d\n", freemem());
	printf(1, "munmap result: %d\n", munmap((uint) memory_area));

	// private file mapping without MAP_POPULATE
	printf(1, "===============Private file mapping without MAP_POPULATE==================\n");
	printf(1, "free memory number: %d\n", freemem());				// number of free space => ?
	memory_area = (char *) mmap(0, 8192, PROT_READ, 0, fd, 0);
	printf(1, "mmap result (first four letters): %s\n", memory_area);	// print memory mapped file
	printf(1, "free memory number: %d\n", freemem());				// number of free space
	printf(1, "munmap result: %d\n", munmap((uint) memory_area));		
	printf(1, "free memory number: %d\n", freemem());

	printf(1, "===============================fork=======================================\n");
	printf(1, "free memory number: %d\n", freemem());
	memory_area = (char *) mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0);
	printf(1, "mmap result (first four letters): %c%c%c%c\n\n", memory_area[0], memory_area[1], memory_area[2], memory_area[3]);
	printf(1, "free memory number before wait: %d\n", freemem());
	fork();
	printf(1, "mmap result (first four letters): %c%c%c%c\n", memory_area[0], memory_area[1], memory_area[2], memory_area[3]);
	printf(1, "free memory number during fork: %d\n", freemem());
	wait();
	printf(1, "free memory number after wait: %d\n", freemem());

	// memory overlapped situation
	printf(1, "============================memory overlapped=============================\n");
	printf(1, "free memory number: %d\n", freemem());				// number of free space
	memory_area = (char *) mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0);
	printf(1, "mmap result: %x\n", (uint) memory_area);
	memory_area2 = (char *) mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0);
	printf(1, "mmap result: %x\n", (uint) memory_area2);
	printf(1, "free memory number: %d\n", freemem());
	printf(1, "munmap result: %d\n", munmap((uint) memory_area));
	printf(1, "munmap result: %d\n", munmap((uint) memory_area2));
	printf(1, "free memory number: %d\n", freemem());

	
	exit();
}
