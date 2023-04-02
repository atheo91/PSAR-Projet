#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <string.h>

#define PORT 10010
#define NB_ESCLAVE_MAX 10
#define NB_PAGE_MAX 1024
#define SIZE_PAGE 16

typedef struct{
	char * proprietaire;
	void * pointer;
}SPAGE;

typedef struct{
	int numPage;
	SPAGE * tabPage[NB_PAGE_MAX];
}SMEMORY;

SMEMORY memoryData;

void * InitMaster(int size) {
	printf("Initialisation Master:\n");
	int fd;
	
	//Ref: https://www.man7.org/linux/man-pages/man3/shm_open.3.html
	//Ouvre un "ficher" en mémoire partagé de taille 0.
	if((fd = shm_open("/memoire", O_CREAT | O_RDWR, 0600)) < 0) {
		perror("shm_open");
		exit(1);
	}

	printf("-Mémoire partagé créée, taille %d octets\n", size);

	//https://www.man7.org/linux/man-pages/man2/ftruncate.2.html
	//Change la taille de la mémoire partagé.
	ftruncate(fd, size);

	printf("-Taille modifier\n");

	void* addr;
	//https://www.man7.org/linux/man-pages/man2/mmap.2.html
	//Virtualize la mémoire partagé.
	if((addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}

	printf("-Virtualization\n");

	memoryData.numPage = size/SIZE_PAGE;
	int modulo = size%SIZE_PAGE;

	//test, page de taille SIZE_PAGE(16) octets
	for(int i = 0; i <= (size/SIZE_PAGE); i++){
		//printf("%d\n", i);
		memoryData.tabPage[i] = (SPAGE *)malloc(sizeof(SPAGE));
		memoryData.tabPage[i]->proprietaire = "\0";
		memoryData.tabPage[i]->pointer = addr+(i*SIZE_PAGE);
		if(i == (size/16) && modulo != 0){
			memoryData.numPage += 1;
			memoryData.tabPage[i+1] = (SPAGE *)malloc(sizeof(SPAGE));
			memoryData.tabPage[i+1]->proprietaire = "\0";
			memoryData.tabPage[i+1]->pointer = addr+((i+1)*SIZE_PAGE);
		}
	}
	
	printf("-%d pages de %d octets\n", memoryData.numPage, SIZE_PAGE);

	printf("-Table info pages\n\n");

	close(fd);
	return addr;
}

/* - Ce rappeller des esclave
- Envoyer une copie au nouveaux esclave
- Invalidité les copie des esclave quand une nouvelle page est écrite*/
void LoopMaster() {
	
	
	
	
	
	
	
	
	
	
	/*int socketfd, socketClientFD;

	struct sockaddr_in addrclt;
	addrclt.sin_family = AF_INET;
	addrclt.sin_port = htons(PORT);
	addrclt.sin_addr.s_addr = INADDR_ANY;

	struct sockaddr_in addrclt2;
	socklen_t sz = sizeof(addrclt2);


	int copyOwner[NB_CLIENTS]; 
	//int lastCopyID = 0;
	//int forking = 0;
	
	//int *currentCountClient = malloc(sizeof(int));
	//*currentCountClient = 0;
	//int numClient = 0;
	//int i = 0;

	if((socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}

	if(bind(socketfd, (struct sockaddr*)&addrclt, sizeof(addrclt)) == -1) {
		perror("bind");
		exit(2);
	}

	if(listen(socketfd, NB_CLIENTS) == -1) {
		perror("listen");
		exit(3);
	}

	while(i < NB_CLIENTS-1){
		copyOwner[i] = -1;
		i++;
	}

	//Loop principale
	while(1){
		Pas vraiment pertinant, a refaire.
		//Si une connection arrive et qu'elle est valide mais également que le nombre d'esclave connecter n'est pas au maximun
		printf("Attente esclave\n");
		if((socketClientFD = accept(socketfd, (struct sockaddr*)&addrclt2, &sz)) != -1 && *currentCountClient != NB_CLIENTS) {
			
			
			printf("Nouveau esclave: %d\n", *currentCountClient+1);
			//Chercher une place dans le tableau des esclave connecter
			i = 0;
			while(i < NB_CLIENTS-1){
				//Place trouver, ecriture du FD dans l'emplacement, envoie d'une copie, nombre de client augmente
				if(copyOwner[i] == -1){
					copyOwner[i] = socketClientFD;
					numClient = i;
					send(copyOwner[numClient], (void *)&i, sizeof(int), 0);
					printf("Copie envoyer\n");
					*currentCountClient = *currentCountClient+1;
					break;
				}	
				i++;		
			}
			
			//Thread pour la gestion du nouveau client
			

			pthread_t thread_id;
    		pthread_create(&thread_id, NULL, slaveProcess, NULL);
    		pthread_join(thread_id, NULL);
			
		}
	}	
	
	shutdown(socketfd, SHUT_RDWR);
	close(socketfd);*/


	// ENVOIE COPIE FICHIER MEMOIRE PARTAGEE
}

void slaveProcess(int *currentCountClient, int FD){
	/*char *bufferRequest = malloc(sizeof(char) * 100);
	bool isRunning = true;

	//Boucle principale du client, reçoit une commande(EX: exit) et fait une action.
	printf("Boucle esclave : %d\n", *currentCountClient);
	while(isRunning == true){
		recv(FD, bufferRequest,sizeof(bufferRequest),0);
		//printf("receive %s\n", bufferRequest);
		if(strcmp(bufferRequest,  "exit") == 0){
			printf("Exit request from : %d\n", FD);
			isRunning = false;
			break;
		}
	}
		
	//Sortie, fermeture du FD, assigne -1 a l'emplacement et reduit le nombre de client
	close(FD);
	*currentCountClient = *currentCountClient-1;
	return;*/
}


void* InitSlave(char* HostMaster) {
	/*int socketfd;

	if((socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}

	struct addrinfo* res;

	if(getaddrinfo(HostMaster, NULL, NULL, &res) != 0) {
		perror("getaddrinfo");
		exit(2);
	}

	struct in_addr ipv4 = ((struct sockaddr_in*) res->ai_addr)->sin_addr;

	freeaddrinfo(res);

	struct sockaddr_in addr;
	memset((void*)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr = ipv4;
	printf("Connecting?\n");
	if(connect(socketfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("connect");
		exit(3);
	}
	printf("Connected\n");
	// LECTURE ET STOCKAGE DANS FICHIER

	int fd;

	if((fd = shm_open("/memoire", O_CREAT | O_RDWR, 0600)) < 0) {
		perror("shm_open");
		exit(1);
	}

	ftruncate(fd, sizeof(int));

	void* adresse;
	if((adresse = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}

	printf("MMAP opened\n");

	//Besoin de demander la taille?? A rajouter
	int *recvBuff = malloc(sizeof(int));

	recv(socketfd, recvBuff, sizeof(int), 0);
	printf("Received copy\n");
	memcpy(adresse, (void *)recvBuff, sizeof(int));
	printf("Copy placed\n");

	char* message = "exit";
	//stopper la connection
	send(socketfd, message, sizeof(message), 0);
	
	return adresse;*/
}

void lock_read(void* adr, int s) {

}

void unlock_read(void* adr, int s) {

}

void lock_write(void* adr, int s) {

}

void unlock_write(void* adr, int s) {

}
