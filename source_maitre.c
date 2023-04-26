#define NB_PAGE_MAX 1024
#define PAGE_SIZE 4096
#define PORT_MAITRE 10036

//Possible REQUETE/RETOUR pendant la communication ECLAVE/MAITRE

#define REQUETE_INIT 0
#define REQUETE_DEMANDE_PAGE 1
#define REQUETE_LOCK_READ 2
#define REQUETE_UNLOCK_READ 3
#define REQUETE_LOCK_WRITE 4
#define REQUETE_UNLOCK_WRITE 5
#define RETOUR_DEMANDE_PAGE_ENVOIE 6
#define RETOUR_DEMANDE_PAGE_VERS_ESCLAVE 7
#define REQUETE_FIN 999

//Donnée d'une page et ses information : le propriétaire, le nombre d'écrivain et de lecteur. ainsi qu'un pointeur vers le début de la page
struct SPAGE{
	int nombre_reader;
	int nombre_writer;
	struct sockaddr * proprietaire;
	void * pointer;
};

//Information sur les esclaves, actuellement connectée, leur FD, et la structure de donnée (IP,port, etc...).
struct ESCLAVE{
	int fd;
	struct sockaddr * info;
	struct ESCLAVE * next;
};

//Donnée de la mémoire partagée, contient des structures page, structures esclave, un mutex, et le nombre de page 
struct SMEMORY{
	int nombre_page;
    int size;
	pthread_mutex_t mutex_maitre;
	struct ESCLAVE * list_esclave;
	struct SPAGE * tab_page[NB_PAGE_MAX];
};

static struct SMEMORY * memory_data; //Mémoire contenant toutes les méta-données de nos page et esclaves

//Initie le maître, créer la zone mémoire, et initie la structure info sur les pages (memory_data)
void * InitMaster(int size) {
	void* addr_zone; //Adresse du début de la mémoire partagé
    int modulo; //Modulo pour voir si la taille est pile un nombre de page, ou entre deux

    printf("Initialisation Master:\n");
	//https://www.man7.org/linux/man-pages/man2/mmap.2.html
	//Mappe notre mémoire partagé de taille size
	if((addr_zone = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(2);
	}
	printf("-Mémoire partagé créée, taille %d octets\n", size);

	//Alloue les méta données(valeur globale) qui nous sert à retenir diverse information (page_size, pointeur sur les pages...)
	if((memory_data = malloc(sizeof(struct SMEMORY))) == NULL){
		perror("Erreur malloc!");
	}
    memory_data->list_esclave = NULL; //NULL est la dernière entrée dans la liste chaînée
    memory_data->size = size;
	memory_data->nombre_page = size/PAGE_SIZE;
	modulo = size%PAGE_SIZE;

	printf("-Table info sur les pages: Création\n");
    //On remplie la valeur globale jusqu'a que l'on à le nombre de page que l'on cherche
	for(int i = 0; i <= (size/PAGE_SIZE); i++){
		if((memory_data->tab_page[i] = (struct SPAGE *)malloc(sizeof(struct SPAGE))) == NULL){
			perror("Erreur malloc!\n");
		}
		memory_data->tab_page[i]->proprietaire = NULL;//Propriétaire NULL veut dire que le maître est le propriétaire
		memory_data->tab_page[i]->pointer = addr_zone+(i*PAGE_SIZE); //Pointeur vers la page, un raccourcie utile
		memory_data->tab_page[i]->nombre_reader = 0; //Nombre lecteurs
		memory_data->tab_page[i]->nombre_writer = 0; //Nombre d'écrivain

		//Initier le surplus, quand on ne remplie pas une page en entière
		if(i == (size/PAGE_SIZE) && modulo != 0){
			memory_data->nombre_page += 1;			
			if((memory_data->tab_page[i+1] = (struct SPAGE *)malloc(sizeof(struct SPAGE))) == NULL){
				perror("Erreur malloc!\n");
			}
			memory_data->tab_page[i+1]->proprietaire = NULL;
			memory_data->tab_page[i+1]->pointer = addr_zone+((i+1)*PAGE_SIZE);
			memory_data->tab_page[i+1]->nombre_reader = 0;
			memory_data->tab_page[i+1]->nombre_writer = 0;
		}
	}
	printf("-%d pages de %d octets\n- Maître initialisée\n\n", memory_data->nombre_page, PAGE_SIZE);
	return addr_zone;
}


//Répond à des requête d'un esclave
static void *slaveProcess(void * copyfd){
	//Détache le processus, pas besoin de join plus tard dans le loopMaster
	pthread_detach(pthread_self());
	
	int fd = *((int *)copyfd); //Descripteur pour la communication de un esclave
	int esclave_end = 0; // Valeur qui signal la fin de la communication
	int requete; //Requete faite / envoyer a l'esclave
	int numero_page; //Numéro de la page demandée(REQUETE_DEMANDE_PAGE)
	int nombre_de_page; // Nombre de page que l'esclave doit rendre (REQUETE_FIN)
	struct ESCLAVE * tempEsclave; // 1: Structure temporaire pour désenregister un esclave de le liste (REQUETE_FIN)
	struct ESCLAVE * tempEsclave2; // 2: Structure temporaire pour désenregister un esclave de le liste (REQUETE_FIN)

	//Verifie si InitMaster à bien été appeler
	if(memory_data == NULL){
		perror("Memoire non-initialisé");
	}

	printf("[fd : %d]Traiment d'un esclave(n°thread) : %ld\n", fd ,syscall(SYS_gettid));
	while(esclave_end != 1){
		//Réinitialise la valeur a chaque boucle.
		requete = -1;
		//Attende de la reception d'une rêquete
		if(recv(fd, &requete, sizeof(int),0)== -1){
			perror("Problème communication 1\n");
            esclave_end = 1;
		}

		//Switch_case pour savoir le comportement à prendre par rapport à la requête
		switch (requete){
		//L'esclave veut s"enregistrer, rentrer dans la liste des esclave, on lui renvoie la taille de la zone mémoire en retour
		case REQUETE_INIT:
			printf("[fd : %d]Initialisation client, envoie de taille\n", fd);
			//Envoie de la taille
			if(send(fd, &memory_data->size, sizeof(long), 0) == -1){
				perror("Problème communication 2\n");
                esclave_end = 1;
			}
			break;
		//L'esclave demande une page car il est dans un défaut de page, et il la nécéssite (ceci assume qu'il à deja les droits, sinon il aurais fait un SEGFAULT avant)
		case REQUETE_DEMANDE_PAGE: 
            printf("[fd : %d]Demande de page (lecture)\n", fd);
			
			//Recevoir le numéro de la page à envoyer
			if(recv(fd, &numero_page, sizeof(int),0)== -1){
				perror("Problème communication 3\n");
                esclave_end = 1;
			}
			printf("[fd : %d]Page numéro %d\n", fd ,numero_page);

			//Qui est le propriétaire de cette page ? Si NULL : maître 
			if(memory_data->tab_page[numero_page]->proprietaire == NULL){
				//Répondre en disant que on envoie la page directement
				requete = RETOUR_DEMANDE_PAGE_ENVOIE;
				if(send(fd, &requete, sizeof(int), 0)== -1){
					perror("Problème communication 4\n");
                	esclave_end = 1;
					goto fin1;
				}

				//Puis on envoye la page entière
				pthread_mutex_lock(&memory_data->mutex_maitre);
				if(send(fd,memory_data->tab_page[numero_page]->pointer, PAGE_SIZE, 0)== -1){
					perror("Problème communication 5\n");
                	esclave_end = 1;
				}
				fin1:
				pthread_mutex_unlock(&memory_data->mutex_maitre);
			//Le propriétaire n'est pas maître
			}else{
				//Dire qu'on envoye les info d'un esclave
				requete = RETOUR_DEMANDE_PAGE_VERS_ESCLAVE;

				//Vérifier que le propriétaire n'est pas lui même (récupere les info sur l'esclave)
				pthread_mutex_lock(&memory_data->mutex_maitre); //Ne faut pas que liste d'esclave soit pas toucher pendant ce temps
				struct ESCLAVE * tempEsclave = memory_data->list_esclave;
				while(tempEsclave->fd != fd){
					tempEsclave = tempEsclave->next;
				}

				//Si il est lui même propriétaire, erreur
				if(memory_data->tab_page[numero_page]->proprietaire == tempEsclave->info){
					printf("[fd : %d]Erreur, propriétaire est celui qui demande???\n", fd);
					requete = -1;
				}

				//Envoie de la demande : eclave à la page
				if(send(fd, &requete, sizeof(int), 0)== -1){
					perror("Problème communication 5\n");
                	esclave_end = 1;
					goto fin2;
				}

				//Envoyer le propriétaire de la page
				if(send(fd,memory_data->tab_page[numero_page]->proprietaire, sizeof(struct sockaddr), 0)== -1){
					perror("Problème communication 5\n");
                	esclave_end = 1;
				}
				fin2:
				pthread_mutex_unlock(&memory_data->mutex_maitre);
			}
			break;
		//L'esclave veut mettre fin à la connexion et se désenregistrer, il doit donc rendre les pages qui lui appartienne
		case REQUETE_FIN: // -> A faire
			printf("[fd : %d]Fin de l'esclave, récupération des pages\n",  fd);
			esclave_end = 1;
			
			//A changer -> Pas terminée -> Calcule et envoie du nombre
			nombre_de_page = 0;
			if(send(fd, &nombre_de_page, sizeof(int), 0) == -1){
				perror("Problème communication 6\n");
                esclave_end = 1;
			}

			//Enlève l'esclave de la liste
			pthread_mutex_lock(&memory_data->mutex_maitre); //La liste ne doit pas être toucher
			//Prend le premier element
			tempEsclave = memory_data->list_esclave;
			//Vérifie que le premier n'est pas null
			if(tempEsclave == NULL){
				printf("Err\n");
				goto end;
			}
			//Si le premier est le rechercher
			else if(tempEsclave->fd == fd){
				//Le premier devient le deuxième
				memory_data->list_esclave = memory_data->list_esclave->next;
				//Libère le premier
				free(tempEsclave);
			}
			//Si plus loin dans la liste
			else{
				//Parcour la liste jusqu'a que le prochain soit la personne rechercher
				while(tempEsclave->next != NULL){
					if(tempEsclave->next->fd == fd){
						break;
					}
					tempEsclave = tempEsclave->next;
				}
				//On stoque le prochain
				tempEsclave2 = tempEsclave->next;
				if(tempEsclave2 == NULL){
					printf("Err2\n");
					pthread_mutex_unlock(&memory_data->mutex_maitre);
					goto end;
				}
				//Le prochain devient celui d'après
				// tempEsclave -> tempEsclave2(celui a surprimer, peut etre null) -> après
				tempEsclave->next = tempEsclave2->next;
				free(tempEsclave2);
			}
			pthread_mutex_unlock(&memory_data->mutex_maitre);
			break;			
		case REQUETE_LOCK_READ:
			break;
		case REQUETE_LOCK_WRITE:
			break;
		case REQUETE_UNLOCK_READ:
			break;
		case REQUETE_UNLOCK_WRITE:
			break;
		default:
			//Cas anormale, rien faire
		}
	}
	end:
	printf("[fd : %d]Fin traitement\n", fd);
	//Ferme la connexion
	shutdown(fd, SHUT_RDWR);
	return NULL;
}


/* Boucle du maître:
- Ce rappeller des esclave
- Envoyer une copie des pages au esclave
- Invalidité les copie des esclave quand une nouvelle page est écrite*/
void LoopMaster() {
	printf("Début de la boucle Maitre :\n");
	int socketEcoutefd, socketEsclaveFD;
	struct ESCLAVE * tempEsclave;
	struct sockaddr_in addrclt2;
	socklen_t sz = sizeof(addrclt2);
	struct sockaddr_in addrclt;
	pthread_t th;
	
	pthread_mutex_init(&memory_data->mutex_maitre, NULL);
        
	//Verifie si InitMaster à bien été appeler
	if(memory_data == NULL){
		perror("Memoire non-initialisé");
	}

	addrclt.sin_family = AF_INET;
	addrclt.sin_port = htons(PORT_MAITRE);
	addrclt.sin_addr.s_addr = INADDR_ANY;

	//Ouverture du socket
	if((socketEcoutefd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}
	printf("-Ouverture d'un socket\n");

	//Lier le port au socket
	if(bind(socketEcoutefd, (struct sockaddr*)&addrclt, sizeof(addrclt)) == -1) {
		perror("bind");
		exit(2);
	}
	printf("-Socket bind\n");

	//Ecoute sur le port
	if(listen(socketEcoutefd, 10) == -1) {
		perror("listen");
		exit(3);
	}
	printf("-Ecoute sur le port\n\n");
	
	//Boucle principale
	//Notes : Ce rappeler des esclaves et leur FD.	
	while(1){
		//Si une connection arrive et qu'elle est validé mais également que le nombre d'esclave connecter n'est pas au maximun
		if((socketEsclaveFD = accept(socketEcoutefd, (struct sockaddr*)&addrclt2, &sz)) != -1) {
			//On trouve la fin de la liste d'esclave, insert le nouveau esclave dans la liste
			pthread_mutex_lock(&memory_data->mutex_maitre);
			if(memory_data->list_esclave == NULL){
				memory_data->list_esclave = malloc(sizeof(struct ESCLAVE));
				memory_data->list_esclave->fd = socketEsclaveFD;
				memory_data->list_esclave->info = (struct sockaddr *)&addrclt2;
				memory_data->list_esclave->next = NULL;
			}else{
				tempEsclave = memory_data->list_esclave;
				while(tempEsclave->next != NULL){
					tempEsclave = tempEsclave->next;
				}
				tempEsclave->next = malloc(sizeof(struct ESCLAVE));
				tempEsclave->next->fd = socketEsclaveFD;
				tempEsclave->next->info = (struct sockaddr *)&addrclt2;
				tempEsclave->next->next = NULL;
			}
			pthread_mutex_unlock(&memory_data->mutex_maitre);

			//Thread pour la gestion du nouveau esclave
			pthread_create(&th, NULL, slaveProcess, (void *)&socketEsclaveFD);	
		}
	}	
	
	//Fermeture du socket
	shutdown(socketEcoutefd, SHUT_RDWR);
}

//Libère la mémoire partagé et les méta donné des pages sur le Maître
void endMaster(void * data, int size){
	//Verifie si InitMaster à bien été appeler
	if(memory_data == NULL){
		perror("Memoire non-initialisé");
	}

	printf("Fin Master:\n");
	//Libère la mémoire partagé
	if(munmap(data, size) == -1){		
		perror("Erreur unmmap!");
	}
	
	//Libère la mémoire de chaque struct SPAGE dans la struct SMEMORY
	for(int i = 0; i <= memory_data->nombre_page; i++){
		free(memory_data->tab_page[i]);
	}

	//Libère la mémoire que prend la struct MEMORY
	free(memory_data);
	printf("- Mémoire libèrer\n\n");
}