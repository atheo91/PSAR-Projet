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
	int fin = 0;
	int select = 0;
	struct data * local_data;

	//Etape 1 : Initialiser la mémoire avec 0 droit dessus. Lancer le handler de défaut de page(thread 1), et une handler de requête distante(thread 2) 
	local_data =  (struct data *) InitSlave(HostMaster);
	
	while(!fin){
		select = 0;
		printf("Action possible :\n- 1 : Lire\n- 2 : Ecrire\n- 3 : Fin\n\nChoix : ");
		scanf("%d", &select);

		switch (select){
		case 1:
			//Etape 2 : Savoir lire en demandant aux maître et gérer les défaut de page en lecture
			printf("Lire Data, choisir le numéro entre 0-1199.\nChoix : ");
			scanf("%d", &select);
			lock_read(&local_data->data[select], sizeof(local_data->data[select]));
			printf("sizeof : %ld\n", sizeof(local_data->data[select]));
			printf("select : %d\n", select);
			printf("%d\nAttente de 5 secondes\n", local_data->data[select]);
			sleep(5);
			unlock_read(&local_data->data[select],  sizeof(local_data->data[select]));
			break;
		case 2:
			//Etape 3 : Savoir écrire et demander aux maitre
			lock_write(local_data, sizeof(struct data));
			local_data->id = 1;
			for(int i = 0; i<= 1199; i++){
				local_data->data[i] = i-20;
			}
			unlock_write(local_data, sizeof(struct data));
			break;
		case 3:
			fin = 1;
			break;
		default:
			break;
		}
	}

	//Etape 4 : Finaliser
	endSlave(local_data, sizeof(struct data));
	return 0;
}
