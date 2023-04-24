// TCP -> Client: socket, connect, send/recv, shutdown, close

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

int main(int argc, char** argv) {
	char * HostMaster = "localhost";
	DATA * localData;

	//Etape 1 : Initialiser la mémoire avec 0 droit dessus. Lancer le handler de défaut de page(thread 1), et une handler de requête distante(thread 2) 
	localData =  (DATA *) InitSlave(HostMaster);
	//Etape 2 : Savoir lire et gérer les défaut de page en lecture
	lock_read(NULL, 0);
	int read = localData->id;


	char * read2 = malloc(sizeof(char));

	read2 = localData->data[1000];
	/*for(int i = 0; i<= 2047; i++){
		localData->data[i] = "Coucou\0";
	}*/
	unlock_read(NULL, 0);

	//Etape 3 : Savoir écrire et gérer les défaut de page en lecture
	/*lock_write(NULL, 0);
	localData->id = 1;
	for(int i = 0; i<= 2047; i++){
		localData->data[i] = "Coucou\0";
	}
	unlock_write(NULL, 0);*/

	//Etape 4 : Finaliser
	endSlave(localData, sizeof(DATA));
	return 0;
}
