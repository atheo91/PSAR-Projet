#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <string.h>
#include <linux/userfaultfd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include "source_esclave.c"
#include "source_maitre.c"

//Donnée répartie example
struct DATA{
	int id;
	int data[14000];
};


void *InitMaster(int size);

void LoopMaster();

void EndMaster(void * data, int size);

void *InitSlave(char *HostMaster);

void lock_read(void *adr, int s);

void unlock_read(void *adr, int s);

void lock_write(void *adr, int s);

void unlock_write(void *adr, int s);

void EndSlave(void * data, int size);