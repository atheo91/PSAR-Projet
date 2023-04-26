#include <stdio.h>
#include "lib.h"

int main() {

	//Etape 1 : Initialisation mémoire partagée (Test de InitMaitre) et la remplir de données.

	struct DATA * local_data = (struct DATA *) InitMaster(sizeof(struct DATA));

	local_data->id = 3;
	
	for(int i = 0; i<= 13999; i++){
		local_data->data[i] = i;
	}

	//Etape 2 : LoopMaster, prendre les requête et gérer la mémoire

	LoopMaster();

	//Etape 3 : Finaliser
	
	endMaster(local_data, sizeof(struct DATA));
	return 0;
}