#include "lib.h"

typedef struct {
	int tab[1200];
	int a;
} t_segpart;

int main() {
	int *seg;
	int i;

	seg = (int *) InitMaster(sizeof(int));

	/* Initialisation du segment */
	*seg = 10;

	/*for (i=0;i<1200;i++)
		seg->tab[i] = i;
	*/
	/* attendre */
	
	LoopMaster(); 

	return 0;
}
