#include "lib.c"

#define MASTERHOST "localhost"

typedef struct {
	int tab[1200];
	int a;
} t_segpart;

int main(){
	int *seg;
	seg = (int *)InitSlave(MASTERHOST);

	printf("Disconnected, ID slave : %d\n", *seg);


	munmap(seg, sizeof(int));


	/*
	// Demande acces en lecture du segment 
	lock_read(&(seg->tab), sizeof(seg->tab));

	// Accès à la page contenant seg->tab[0] 
	printf("%d\n", seg->tab[0]);

	// Accès à la page contenant seg->tab[1050] 
	printf("%d\n", seg->tab[1000]);

	// Libérer l'accès en lecture
	unlock_read(&(seg->tab), sizeof(seg->tab));

	// Demande acces en écriture du segment 
	lock_write(&(seg->a), sizeof(seg->a));
	seg->a = 10;

	// Libérer l'accès en ecriture 
	unlock_write(&(seg->a), sizeof(seg->a));
	*/
	// ...

	return 0;
}
