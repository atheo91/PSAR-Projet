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
#include <linux/userfaultfd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define PORT_MAITRE 10011
#define PORT_ESCLAVE 10012
#define NB_ESCLAVE_MAX 10
#define NB_PAGE_MAX 1024

//Donnée répartie example
typedef struct{
	int id;
	char * data[2048];
}DATA;
//Donnée d'une page
typedef struct{
	int numReader;
	struct sockaddr * proprietaire;
	void * pointer;
}SPAGE;
//Donnée de la mémoire partagée
typedef struct{
	long sizePage;
	int numPage;
	SPAGE * tabPage[NB_PAGE_MAX];
}SMEMORY;

static SMEMORY * memoryData;

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

	//Alloue la mémoire partagée

	if((memoryData = malloc(sizeof(SMEMORY))) == NULL){
		perror("Erreur malloc!");
	}

	/*printf("%p\n", memoryData);
	printf("%p,%d",&memoryData->numPage, memoryData->numPage);*/

	//Assigne la taille d'une page par rapport au système du maître
	memoryData->sizePage = sysconf(_SC_PAGESIZE);
	if (memoryData->sizePage == -1) {
        perror("sysconf/pagesize");
        exit(1);
    }

	memoryData->numPage = size/memoryData->sizePage;
	int modulo = size%memoryData->sizePage;

	//test, page de taille SIZE_PAGE octets
	for(int i = 0; i <= (size/memoryData->sizePage); i++){
		if((memoryData->tabPage[i] = (SPAGE *)malloc(sizeof(SPAGE))) == NULL){
			perror("Erreur malloc!");
		}
		memoryData->tabPage[i]->proprietaire = NULL;
		memoryData->tabPage[i]->pointer = addr+(i*memoryData->sizePage);
		memoryData->tabPage[i]->numReader = 0;
		//Initier le surplus, qui ne remplie pas une page en entière
		if(i == (size/memoryData->sizePage) && modulo != 0){
			memoryData->numPage += 1;			
			if((memoryData->tabPage[i+1] = (SPAGE *)malloc(sizeof(SPAGE))) == NULL){
				perror("Erreur malloc!");
			}
			memoryData->tabPage[i+1]->proprietaire = NULL;
			memoryData->tabPage[i+1]->pointer = addr+((i+1)*memoryData->sizePage);
			memoryData->tabPage[i]->numReader = 0;
		}
	}
	printf("-%d pages de %ld octets\n", memoryData->numPage, memoryData->sizePage);

	printf("-Table info pages\n\n");

	close(fd);
	return addr;
}

struct request{
	int fd;
	/*Numéro de la rêquete désirer, le nombre déterminera l'action prise par slaveProcess
		-> 0 : Initialisation d'un nouveau esclave, en vérité ne sert a rien, juste pour tester les requête et réponse
	*/
	int numRequest;
};

//Répond à une requête d'un esclave
static void *slaveProcess(void * param){
	//Détache le processus, pas besoin de join plus tard dans le loopMaster
	pthread_detach(pthread_self());

	struct request * info = param;

	printf("-Traiment d'un esclave(n°thread) : %ld\n",  syscall(SYS_gettid));

	//Switch pour savoir le comportement à prendre
	switch (info->numRequest){
	case 0:
		printf("-Initialisation client, envoie de taille : %ld\n",  syscall(SYS_gettid));
		char * sent = malloc(sizeof(char) * 8);
		sprintf(sent, "%ld", sizeof(DATA));
		send(info->fd, sent, strlen(sent), 0);
		break;
	case 1:
		//Autre requête à compléter
	default:
		break;
	}

	printf("-Fin traitement : %ld\n",  syscall(SYS_gettid));
	return NULL;
}


/* Boucle du maître:
- Ce rappeller des esclave
- Envoyer une copie des pages au esclave
- Invalidité les copie des esclave quand une nouvelle page est écrite*/
void LoopMaster() {
	printf("Début de la boucle Maitre :\n");
	struct request * param = malloc(sizeof(struct request));
	int socketfd, socketEsclaveFD;
	pthread_t th;

	//Verifie si InitMaster à bien été appeler
	if(memoryData == NULL){
		perror("Memoire non-initialisé");
	}

	struct sockaddr_in addrclt;
	addrclt.sin_family = AF_INET;
	addrclt.sin_port = htons(PORT_MAITRE);
	addrclt.sin_addr.s_addr = INADDR_ANY;

	struct sockaddr_in addrclt2;
	socklen_t sz = sizeof(addrclt2);

	//Ouverture du socket
	if((socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}
	printf("-Ouverture d'un socket\n");

	//Bind le port au socket
	if(bind(socketfd, (struct sockaddr*)&addrclt, sizeof(addrclt)) == -1) {
		perror("bind");
		exit(2);
	}
	printf("-Socket bind\n");

	//Ecoute sur le port
	if(listen(socketfd, NB_ESCLAVE_MAX) == -1) {
		perror("listen");
		exit(3);
	}
	printf("-Ecoute sur le port\n\n");
	
	//Boucle principale
	while(1){
		//Si une connection arrive et qu'elle est validé mais également que le nombre d'esclave connecter n'est pas au maximun
		if((socketEsclaveFD = accept(socketfd, (struct sockaddr*)&addrclt2, &sz)) != -1) {
			char numRequestS[3];
			recv(socketEsclaveFD, numRequestS, 3,0);
			param->fd = socketEsclaveFD;
			param->numRequest = atoi(numRequestS);
			printf("Nouveau client :\n-Numéro requête esclave : %d\n", param->numRequest);
			
			//Thread pour la gestion du nouveau esclave
			pthread_create(&th, NULL, slaveProcess, (void *)param);	
		}
	}	
	
	//Unbind
	shutdown(socketfd, SHUT_RDWR);
	//Fermeture du socket
	close(socketfd);
}

//Libère la mémoire partagé et les méta donné des pages sur le Maître
void endMaster(void * data, int size){
	printf("Fin Master:\n");
	//Libère la mémoire partagé
	if(munmap(data, size) == -1){
		perror("Erreur unmmap!");
	}
	
	//Libère la mémoire de chaque structure PAGE dans la struct MEMORY
	for(int i = 0; i <= memoryData->numPage; i++){
		free(memoryData->tabPage[i]);
	}

	//Libère la mémoire que prend la struct MEMORY
	free(memoryData);
	printf("- Mémoire libèrer\n\n");
}







//Thread de demande d'une page de cette esclave à l'esclave qui demande(adresse est le début de la mmap locale)
//-> A faire : L'arrêter après la fin de l'esclave
void* slaveLoop(void * adresse){
	pthread_detach(pthread_self());

	int socketfd, socketEsclaveFD;

	struct sockaddr_in addrclt;
	addrclt.sin_family = AF_INET;
	addrclt.sin_port = htons(PORT_ESCLAVE);
	addrclt.sin_addr.s_addr = INADDR_ANY;

	struct sockaddr_in addrclt2;
	socklen_t sz = sizeof(addrclt2);

	//Ouverture du socket
	if((socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}

	//Bind le port au socket
	if(bind(socketfd, (struct sockaddr*)&addrclt, sizeof(addrclt)) == -1) {
		perror("bind");
		exit(2);
	}

	//Ecoute sur le port
	if(listen(socketfd, NB_ESCLAVE_MAX) == -1) {
		perror("listen");
		exit(3);
	}

	printf("-Thread esclave vers esclave initialiser\n\n");
	while(1){
		if((socketEsclaveFD = accept(socketfd, (struct sockaddr*)&addrclt2, &sz)) != -1) {
			//Traitement de la demande d'un esclave demandant une page à cette esclave
		}
	}
	return NULL;
}

//Initialisation de l'esclave
void* InitSlave(char* HostMaster) {
	printf("Initialisation Esclave:\n");
	int socketfd;

	if((socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}

	struct addrinfo* res;

	if(getaddrinfo(HostMaster, NULL, NULL, &res) != 0) {
		perror("getaddrinfo");
		exit(2);
	}
	printf("-Recherche Maitre\n");

	struct in_addr ipv4 = ((struct sockaddr_in*) res->ai_addr)->sin_addr;

	freeaddrinfo(res);

	struct sockaddr_in addr;
	memset((void*)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_MAITRE);
	addr.sin_addr = ipv4;
	printf("-Connexion Maitre\n");

	//Tant que pas connecter au maître on réessaye
	while(connect(socketfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("Connection échouer");
		sleep(2);
	}
	printf("-Connecter\n");

	// Test de demande de rêquete ( 0 : demande de la taille de la mmap)
	char *sendRequest = "0\0";
	char recvSize[10];

	send(socketfd, sendRequest, strlen(sendRequest), 0);

	recv(socketfd, recvSize, 10, 0);

	int size = atoi(recvSize);
	printf("-Taille reçu :%d\n", size);

	int fd;

	//Ouverture de la mémoire partager de taille obtenue précédamment
	if((fd = shm_open("/memoire", O_CREAT | O_RDWR, 0600)) < 0) {
		perror("shm_open");
		exit(1);
	}

	ftruncate(fd, size);

	void* adresse;
	//Aucun droit d'écriture/lecture
	if((adresse = mmap(NULL, size, PROT_NONE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}
	printf("-Mémoire partagé locale ouverte\n");

	//Démarrer un thread pour les demande de pages des autres esclaves
	pthread_t th;
	pthread_create(&th, NULL, slaveLoop, (void *)adresse);

	//A faire : Démarrer un thread qui s'ocupera des défaut de page avec userfaultfd
	
	return adresse;
}


/*Handle des default pages :
* https://noahdesu.github.io/2016/10/10/userfaultfd-hello-world.html
*
*/


void lock_read(void* adr, int s) {

}

void unlock_read(void* adr, int s) {

}

void lock_write(void* adr, int s) {

}

void unlock_write(void* adr, int s) {

}
