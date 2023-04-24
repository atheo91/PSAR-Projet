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
#include <signal.h>

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
	int numWriter;
	struct sockaddr * proprietaire;
	void * pointer;
}SPAGE;
//Donnée de la mémoire partagée
typedef struct{
	long sizePage;
	int numPage;
	SPAGE * tabPage[NB_PAGE_MAX];
}SMEMORY;
//Paramètre pour le userfaultfd
struct params {
    int uffd;
    long page_size;
	int nombre_page;
	void * pointeurZoneMemoire;
};

//Variable globale maitre
static SMEMORY * memoryData;

//Variable globale esclave
static volatile int stopThreads;
static volatile int writeRights;


void * InitMaster(int size) {
	printf("Initialisation Master:\n");
	int fd;
	void* addr;

	//https://www.man7.org/linux/man-pages/man2/mmap.2.html
	//Virtualize la mémoire partagé.
	if((addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}

	printf("-Mémoire partagé créée, taille %d octets\n", size);

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
		memoryData->tabPage[i]->numWriter = 0;
		//Initier le surplus, qui ne remplie pas une page en entière
		if(i == (size/memoryData->sizePage) && modulo != 0){
			memoryData->numPage += 1;			
			if((memoryData->tabPage[i+1] = (SPAGE *)malloc(sizeof(SPAGE))) == NULL){
				perror("Erreur malloc!");
			}
			memoryData->tabPage[i+1]->proprietaire = NULL;
			memoryData->tabPage[i+1]->pointer = addr+((i+1)*memoryData->sizePage);
			memoryData->tabPage[i+1]->numReader = 0;
			memoryData->tabPage[i+1]->numWriter = 0;
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
	case 1: //Lecteur : demande de page
		printf("-Demande de page (lecture): %ld\n",  syscall(SYS_gettid));
		//Recevoir le numéro de la page demandée
		int * numPage = malloc(sizeof(int));
		recv(info->fd, numPage, sizeof(int),0);
		printf("-Page numéro %d : %ld\n",  *numPage, syscall(SYS_gettid));

		//Vérifier que aucun écrivain (A déplacer dans la requête lock_read)
		/*while(memoryData->tabPage[*numPage]->numWriter != 0){
			sleep(1);
		}*/

		//Qui est le propriétaire ? Si NULL : maître 
		memoryData->tabPage[*numPage]->numReader++;
		if(memoryData->tabPage[*numPage]->proprietaire == NULL){
			//Dire qu'on envoye la page
			send(info->fd, (void *)0, sizeof(int), 0);
			//Envoyer la page entière
			send(info->fd,memoryData->tabPage[*numPage]->pointer, memoryData->sizePage, 0);
		}else{
			//Dire qu'on envoye les info d'un esclave
			send(info->fd, (void *)1, sizeof(int), 0);
			//Envoyer le propriétaire
			send(info->fd,memoryData->tabPage[*numPage]->proprietaire, sizeof(struct sockaddr), 0);
		}
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
	//Notes : Ce rappeler des esclaves et leur FD.	
	while(1){
		//Si une connection arrive et qu'elle est validé mais également que le nombre d'esclave connecter n'est pas au maximun
		if((socketEsclaveFD = accept(socketfd, (struct sockaddr*)&addrclt2, &sz)) != -1) {
			int * numRequest = malloc(sizeof(int));
			
			recv(socketEsclaveFD, numRequest, sizeof(int),0);
			param->fd = socketEsclaveFD;
			param->numRequest = *numRequest;
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
	while(stopThreads ==0){
		if((socketEsclaveFD = accept(socketfd, (struct sockaddr*)&addrclt2, &sz)) != -1) {
			//Traitement de la demande d'un esclave demandant une page à cette esclave

		}
	}
	return NULL;
}

static void *handleDefault(void * arg){
	struct params *p = arg;
    long page_size = p->page_size;
    char buf[page_size];

    for (;;) {
        struct uffd_msg msg;

        //
        struct pollfd pollfd[1];
        pollfd[0].fd = p->uffd;
        pollfd[0].events = POLLIN|POLLOUT;	// les évènements attendus ; POLLIN:

        // wait for a userfaultfd event to occur
        int pollres = poll(pollfd, 1, 2000);

        if (stopThreads){
			printf("Stop userfaultfd handle");
			goto end;
		}
            

        switch (pollres) {
        case -1:
            perror("poll/userfaultfd");
            goto end;
        case 0:
            continue;
        case 1:
            break;
        default:
            fprintf(stderr, "unexpected poll result\n");
            goto end;
        }

        if (pollfd[0].revents & POLLERR) {
            fprintf(stderr, "pollerr\n");
            goto end;
        }


        int readres = read(p->uffd, &msg, sizeof(msg));
        if (readres == -1) {
            if (errno == EAGAIN){
                printf("eagain");
                continue;
            }
            perror("read/userfaultfd");
            goto end;
        }

        if (readres != sizeof(msg)) {
            fprintf(stderr, "invalid msg size\n");
            goto end;
        }

        // handle the page fault by copying a page worth of byte
        printf("Traitement fault\n");
        if (msg.event & UFFD_EVENT_PAGEFAULT) {
            long long addr = msg.arg.pagefault.address;
            
            // UFFDIO_WRITEPROTECT */
            if((msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) && !(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP)) {
                printf("ecriture\n");
                //Laisser l'utilisateur lire la page
                struct uffdio_copy copy;
                copy.src = (long long)buf;
                copy.dst = (long long)addr;
                copy.len = page_size;
                copy.mode = UFFDIO_COPY_MODE_WP;

                //UFFDIO_COPY_MODE_WP
                if (ioctl(p->uffd, UFFDIO_COPY, &copy) == -1) {
                    perror("ioctl/copy");
                    goto end;
                }
                /*struct uffdio_writeprotect wp;
                wp.range.start = addr;
                wp.range.len = page_size;
                wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

                if(ioctl(p->uffd, UFFDIO_WRITEPROTECT, &wp) == -1){
                    perror("ioctl(UFFDIO_WRITEPROTECT)");
                }*/
            }else if(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
                printf("proteger\n");   
                struct uffdio_writeprotect wp;
                wp.range.start = addr;
                wp.range.len = page_size;
                wp.mode = 0;

				sleep(1);
				if(writeRights == 0){
					printf("aaaaaaaaaaaaaaaaaaaaaaaaaa\n"); 
					raise(SIGSEGV);
				}

                if(ioctl(p->uffd, UFFDIO_WRITEPROTECT, &wp) == -1){
                    perror("ioctl(UFFDIO_WRITEPROTECT)");
					goto end;
                }
            }else{
                printf("lecture\n");
				int offset = addr - (long int)p->pointeurZoneMemoire;

				printf("offset : %d\n", offset);

                //Laisser l'utilisateur lire la page
                struct uffdio_copy copy;
                copy.src = (long long)buf;
                copy.dst = (long long)addr;
                copy.len = page_size;
                copy.mode = UFFDIO_COPY_MODE_WP;

                //UFFDIO_COPY_MODE_WP
                if (ioctl(p->uffd, UFFDIO_COPY, &copy) == -1) {
                    perror("ioctl/copy");
                    goto end;
                }
                /*struct uffdio_writeprotect wp;
                wp.range.start = addr;
                wp.range.len = page_size;
                wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

                if(ioctl(p->uffd, UFFDIO_WRITEPROTECT, &wp) == -1){
                    perror("ioctl(UFFDIO_WRITEPROTECT)");
                }*/
            }

            // Ce bit est toujours accompagné avec le bit UFFD_PAGEFAULT_FLAG_WRITE

            //L'adresse de la page avec un défaut
            

            //ACTION SUR LA PAGE

           
            
        }
	}

	end:
	if (ioctl(p->uffd, UFFDIO_UNREGISTER, p->nombre_page * p->page_size)) {
        fprintf(stderr, "ioctl unregister failure\n");
    }
	return NULL;
}

//Initialisation de l'esclave
void* InitSlave(char* HostMaster) {
	printf("Initialisation Esclave:\n");
	int uffd;					// Descripteur de fichier d'erreur de l'utilisateur
	int socketfd;

	writeRights = 0;

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

	// Enregistrer cette esclave aux maitre( 0 : demande de la taille de la mmap)
	int *sendRequest = malloc(sizeof(int));
	char recvSize[10];

	*sendRequest = 0;
	send(socketfd, sendRequest, sizeof(int), 0);

	recv(socketfd, recvSize, 10, 0);

	int size = atoi(recvSize);
	printf("-Taille reçu :%d\n", size);

	void* adresse;
	//Aucun droit d'écriture/lecture
	if((adresse = mmap(NULL, size, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}
	printf("-Mémoire partagé locale ouverte\n");

	stopThreads = 0;

	//Démarrer un thread pour les demande de pages des autres esclaves
	pthread_t th;
	if(pthread_create(&th, NULL, slaveLoop, (void *)adresse)!= 0) {
    	perror("pthread1");
    	exit(1);
    }


    /*--------------------------------------------------------------------MISE EN PLACE DE USERFAULTFD-----------------------------------------------------------------*/
    // Créer et activer l'objet userfaultfd (nouvelle mise en place de userfaultfd)
    // -> _NR_userfaultfd : numéro de l'appelle système
    // -> O_CLOEXEC : (FLAG) Permet le multithreading
    // -> O_NONBLOCK : (FLAG) Ne pas bloquer durant POLL
    uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (uffd == -1) {
        perror("syscall/userfaultfd");
        exit(1);
    }

    // Activer la version de l'api et vérifier les fonctionnalités
    struct uffdio_api uffdio_api;
    uffdio_api.api = UFFD_API;

    //IOCTL : permet de communiquer entre le mode user et kernel. On demande une requête de type UFFDIO_API qui va remplir notre structure uffdio_api
    if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
        perror("ioctl/uffdio_api");
        exit(1);
    }

    //Vérifier que l'API récupérer est la bonne, que notre système est compatible avec userfaultfd API
    if (uffdio_api.api != UFFD_API) {
        fprintf(stderr, "unsupported userfaultfd api (1)\n");
        exit(1);
    }else if (!(uffdio_api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP)) {
    	fprintf(stderr, "unsupported userfaultfd api (3)\n");
    	exit(1);
    }

	int num_page = size / 4096;
    if(size % 4096 != 0){
		num_page++;
	}
    // Enregistrer la plage de mémoire du mappage que nous venons de créer pour qu'elle soit gérée par l'objet userfaultfd
    // UFFDIO_REGISTER_MODE_MISSING: l'espace utilisateur reçoit une notification de défaut de page en cas d'accès à une page manquante (les pages qui n'ont pas encore fait l'objet d'une faute)
    // UFFDIO_REGISTER_MODE_WP: l'espace utilisateur reçoit une notification de défaut de page lorsqu'une page protégée en écriture est écrite. Nécéssaire pour le handle de défaut de page
    struct uffdio_register uffdio_register;
    //Début de la zone mémoire (pointeur)
    uffdio_register.range.start = (unsigned long)adresse;
    //Sa taille (nombre de page * taille d'une page) si ce n'est pas un multiple de page, UFFDIO_REGISTER ne marchera pas
    uffdio_register.range.len = num_page * 4096;
    uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;


	printf("-Taille couverte par userfaultfd : %lld (nombre de page : %d)\n", uffdio_register.range.len,num_page);

    //Enregistre et setup notre userfaultfd avec la plage donnée (communication avec le noyau)
    if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		if(errno == EINVAL)
			perror("Un problème...");
		perror("ioctl/uffdio_register");
		exit(1);
	}

	//Démarrer un thread pour gérer les défaut de pages
	struct params p;
    p.uffd = uffd;
	p.nombre_page = num_page;
    p.page_size = 4096;
	p.pointeurZoneMemoire = adresse;

	pthread_t thhandle;
	if(pthread_create(&thhandle, NULL, handleDefault, &p) != 0) {
    	perror("pthread2");
    	exit(1);
    }
	sleep(1);

	return adresse;
}



/*Handle des default pages :
* https://nwriteRightsoahdesu.github.io/2016/10/10/userfaultfd-hello-world.html
*
*/


void lock_read(void* adr, int s) {
	
}

void unlock_read(void* adr, int s) {
	
}

void lock_write(void* adr, int s) {
	writeRights = 1;
}

void unlock_write(void* adr, int s) {
	writeRights = 0;
}

//Libère la mémoire partagé, dit au maître de devenir propriétaire des pages???
void endSlave(void * data, int size){
	printf("Fin Esclave\n");
	stopThreads = 1;

	//Libère la mémoire partagé
	if(munmap(data, size) == -1){
		perror("Erreur unmmap!");
	}
}