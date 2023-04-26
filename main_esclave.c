#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "lib.h"

int main() {
	char * HostMaster = "localhost";
	struct DATA * local_data;

	//Etape 1 : Initialiser la mémoire avec 0 droit dessus. Lancer le handler de défaut de page(thread 1), et une handler de requête distante(thread 2) 
	local_data =  (struct DATA *) InitSlave(HostMaster);
	
	//Etape 2 : Savoir lire en demandant aux maître et gérer les défaut de page en lecture
	lock_read(local_data, sizeof(struct DATA));
	printf("%d\n", local_data->id);
	for(int i = 0; i<= 13999; i++){
		printf("%d\n",local_data->data[i]);
	}
	unlock_read(local_data, sizeof(struct DATA));

	//Etape 3 : Savoir écrire et demander aux maitre
	lock_write(local_data, sizeof(struct DATA));
	local_data->id = 1;
	for(int i = 0; i<= 13999; i++){
		local_data->data[i] = i-20;
	}
	unlock_write(local_data, sizeof(struct DATA));

	//Etape 4 : Finaliser
	endSlave(local_data, sizeof(struct DATA));
	return 0;
}
