#include "lib.c"

void *InitMaster(int size);

void LoopMaster();

void *InitSlave(char *HostMaster);

void lock_read(void *adr, int s);

void unlock_read(void *adr, int s);

void lock_write(void *adr, int s);

void unlock_write(void *adr, int s);
