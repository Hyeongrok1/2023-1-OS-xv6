#include "types.h"
#include "user.h"
#include "stat.h"
#include "param.h"

int main()
{	
	printf(1, "freemem: %d\n", freemem());
	int fd = open("README", 0);
	// printf(1, "mmap result: %x\n", mmap(0, 8192, PROT_READ, MAP_POPULATE|MAP_ANONYMOUS, -1, 0));
	// printf(1, "freemem: %d\n", freemem());
	// printf(1, "munmap result: %d\n", munmap(0));
	char *without_populate = (char *) mmap(8192, 8192, PROT_READ, 0, fd, 0);
	printf(1, "mmap result: %s\n", without_populate);
	printf(1, "freemem: %d\n", freemem());
	printf(1, "munmap result: %d\n", munmap(8192));
	printf(1, "freemem: %d\n", freemem());
	// printf(1, "mmap result: %x\n", mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0));
	// printf(1, "mmap result: %x\n", mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 0));
	// printf(1, "freemem: %d\n", freemem());
	// printf(1, "munmap result: %d\n", munmap(0));
	// printf(1, "munmap result: %d\n", munmap(0));
	// printf(1, "freemem: %d\n", freemem());
	exit();
}
