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

	//Etape 3 : Savoir écrire et gérer les défaut de page en lecture

	//Etape 4 : Finaliser
	munmap(localData, sizeof(DATA));
	return 0;
}
