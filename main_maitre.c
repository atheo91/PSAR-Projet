#include <stdio.h>
#include "lib.h"

int main() {

	//Etape 1 : Initialisation mémoire partagée (Test de InitMaitre) et la remplir de données.

	struct data * local_data = (struct data *) InitMaster(sizeof(struct data));

	local_data->id = 1234;
	
	for(int i = 0; i<= 1199; i++){
		local_data->data[i] = i;
	}

	//Etape 2 : LoopMaster, prendre les requête et gérer la mémoire

	LoopMaster();

	//Etape 3 : Finaliser
	
	endMaster(local_data, sizeof(struct data));
	return 0;
}