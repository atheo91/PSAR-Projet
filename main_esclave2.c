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
#include <time.h>

#include "lib.h"

#define TAB_SIZE 10000

int main() {
	char * HostMaster = "localhost";
	int fin = 0;
	int select = 0;
	int valeur = 0;
	struct data * local_data;

	//Etape 1 : Initialiser la mémoire avec 0 droit dessus. Lancer le handler de défaut de page(thread 1), et une handler de requête distante(thread 2) 
	local_data =  (struct data *) InitSlave(HostMaster);
	srand(time(NULL));

	while(1){

		select = rand()%5 +1;
		switch (select){
		case 1:
            select = rand() % TAB_SIZE;
			printf("Lire data singulier, numéro : %d\n", select);
			lock_read(&local_data->data[select], sizeof(local_data->data[select]));
			printf("Valeur : %d\n",local_data->data[select]);
			printf("\nAttente de 1 seconde\n");
			//sleep(2);
			unlock_read(&local_data->data[select], sizeof(local_data->data[select]));
			break;
		case 2:
			//Etape 3 : Savoir écrire et demander aux maitre
            select = rand() % TAB_SIZE;
            valeur = rand() % 10000;
			printf("Ecrire data singulier, numéro : %d, ecrire : %d\n", select, valeur);
			lock_write(&local_data->data[select], sizeof(local_data->data[select]));
			local_data->data[select] = valeur;
			printf("\nAttente de 1 seconde\n");
			//sleep(2);
			unlock_write(&local_data->data[select], sizeof(local_data->data[select]));
			break;
		case 3:
			lock_read(local_data, sizeof(struct data));
			for(int i = 0; i< TAB_SIZE; i++){
				printf("%d ", local_data->data[i]);
			}
			printf("\nAttente de 1 seconde\n");
			//sleep(2);
			unlock_read(local_data, sizeof(struct data));
			break;
		case 4:
            valeur = rand() % 10000;
			lock_write(local_data, sizeof(struct data));
			for(int i = 0; i< TAB_SIZE; i++){
				local_data->data[i] = valeur;
			}
			printf("\nAttente de 1 seconde\n");
			//sleep(2);
			unlock_write(local_data, sizeof(struct data));
			break;
		default:
			break;
		}
	}

	//Etape 4 : Finaliser
	endSlave(local_data, sizeof(struct data));
	return 0;
}
