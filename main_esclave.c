#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "lib.h"

#define LECTURE				1
#define ECRITURE			2
#define ECRITURE_ENTIERE	3
#define LECTURE_ENTIERE		4
#define TERMINAISON			5
#define ERROR				-1

int main(int argc, char **argv) {
	char * HostMaster = "localhost";
	struct data * local_data;
	int terminaison=0, v1=NONE, v2=NONE;
	char *p, tmp[50];
	long choix;

	if(argc == 2){
		HostMaster = argv[1];
		printf("Nom maître : %s\n", argv[1]);
	} else{
		HostMaster = "localhost";
		printf("Comportement par défaut nom maître : localhost\n");
	}

	// Etape 1 : Initialiser la mémoire avec 0 droit dessus. Lancer le handler de défaut de page(thread 1), et une handler de requête distante(thread 2)
	local_data =  (struct data*) InitSlave(HostMaster);

	while(!terminaison) {
		choix = NONE;

		do {
			printf("\n-----------------------\n");
			printf("Tableau de bord:\n - 1: Lecture\n - 2: Ecriture\n - 3: Tout lire\n - 4: Tout écrire\n - 5: Terminaison\n\nVotre choix ? ");
			fgets(tmp, sizeof(tmp), stdin);
			if((choix = strtol(tmp, &p, 10)) == 0) {
				if(errno == EINVAL) {
					choix = ERROR;
				}
			}
		} while(choix == ERROR || *p != '\n');

		switch(choix) {
		case LECTURE:
			// Etape 2 : Savoir lire en demandant aux maître et gérer les défaut de page en lecture
			printf("Lecture d'une donnée au sein du tableau 'data'...\nVeuillez choisir un entier [0 - %d[.\n> Votre choix ? ", TAB_SIZE);
			do {
				fgets(tmp, sizeof(tmp), stdin);
				if((v1 = strtol(tmp, &p, 10)) == 0) {
					if(p == tmp)
						v1 = ERROR;
				}
				if(v1 < 0 || v1 >= TAB_SIZE) {
					printf("Veuillez choisir un entier conforme (entre [0 - %d[)...\nVotre choix ? ", TAB_SIZE);
				}
			} while(v1 == ERROR || *p != '\n' || v1 < 0 || v1 >= TAB_SIZE);

			lock_read(&local_data->data[v1], sizeof(local_data->data[v1]));
			printf("Résultat: data[%d] = %d\nAttendre de 5 secondes...\n", v1, local_data->data[v1]);
			sleep(5);
			unlock_read(&local_data->data[v1],  sizeof(local_data->data[v1]));
			break;

		case ECRITURE:
			// Etape 3 : Savoir écrire et demander aux maitre
			printf("Ecriture d'une donnée au sein du tableau 'data'...\nVeuillez choisir un entier [0 - %d[.\n> Votre choix ? ", TAB_SIZE);
			do {
				fgets(tmp, sizeof(tmp), stdin);
				if((v1 = strtol(tmp, &p, 10)) == 0) {
					if(p == tmp)
						v1 = ERROR;
				}
				if(v1 < 0 || v1 >= TAB_SIZE) {
					printf("Veuillez choisir un entier conforme (entre [0 - %d[)...\nVotre choix ? ", TAB_SIZE);
				}
			} while(v1 == ERROR || *p != '\n' || v1 < 0 || v1 >= TAB_SIZE);

			printf("Quelle sera la nouvelle valeur pour 'data[%d]' ? ", v1);
			do {
				fgets(tmp, sizeof(tmp), stdin);
				if((v2 = strtol(tmp, &p, 10)) == 0) {
					if(p == tmp)
						v2 = ERROR;
				}
			} while(v2 == ERROR || *p != '\n');

			lock_write(&local_data->data[v1], sizeof(local_data->data[v1]));
			local_data->data[v1] = v2;
			printf("\nAttente de 5 secondes...\n");
			sleep(5);
			unlock_write(&local_data->data[v1], sizeof(local_data->data[v1]));
			break;

		case LECTURE_ENTIERE:
			lock_read(local_data, sizeof(struct data));
			for(int i = 0; i< TAB_SIZE; i++){
				printf("%d ", local_data->data[i]);
			}
			printf("\nAttendre de 5 secondes...\n");
			sleep(5);
			unlock_read(local_data, sizeof(struct data));
			break;

		case ECRITURE_ENTIERE:
			lock_write(local_data, sizeof(struct data));
			for(int i = 0; i< TAB_SIZE; i++){
				local_data->data[i] = i+10;
			}
			printf("\nAttendre de 5 secondes...\n");
			sleep(5);
			unlock_write(local_data, sizeof(struct data));
			break;

		case TERMINAISON:
			terminaison = 1;
			break;

		default:
			break;
		}
	}

	// Etape 4 : Finaliser
	endSlave(local_data, sizeof(struct data));
	return 0;
}
