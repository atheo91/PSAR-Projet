#define PAGE_SIZE 4096
#define PORT_MAITRE 10049

//Possible REQUETE/RETOUR pendant la communication ECLAVE/MAITRE

#define REQUETE_INIT 1
#define REQUETE_DEMANDE_PAGE 2
#define REQUETE_LOCK_READ 3
#define REQUETE_UNLOCK_READ 4
#define REQUETE_LOCK_WRITE 5
#define REQUETE_UNLOCK_WRITE 6
#define RETOUR_DEMANDE_PAGE_ENVOIE 7
#define RETOUR_DEMANDE_PAGE_VERS_ESCLAVE 8
#define ACK				100
#define REQUETE_FIN 999


//Donnée de droits sur les pages dans la zone mémoire
struct memoire_pages{
	int validite;
	int droit_write;
	int droit_read;
	struct memoire_pages * next;
};
//Paramètre pour le userfaultfd
struct handle_params {
    int uffd;
	int nombre_page;
	void * pointeurZoneMemoire;
};

struct message {
	int type;				// type de requête: LOCK_READ ou UNLOCK_READ (+ tard: LOCK_WRITE ou UNLOCK_WRITE aussi)
	int debut_page;			// [LOCK,UNLOCK] envoie : numéro de la page où la donnée chercher est.
	int fin_page;			// [LOCK,UNLOCK] envoie : numéro de la page final.
	int port;				// [INIT] envoie : port où l'esclave écoute.
};

//Variable globale esclave
static struct memoire_pages * memoire;
static pthread_mutex_t mutex_esclave;
static pthread_t pth_esclave; 
static pthread_t pth_userfaultfd; 
static int maitre_fd;
void *region;				// Début de la région gérée par userfaultfd

//Thread de communication esclave/esclave, permet d'envoyer les page qu'il est propriétaire (prend l'adresse de la mémoire partagée en paramètre)
void* LoopSlave(void * adresse){
	pthread_detach(pthread_self()); //Détache le thread, pour qu'il se finise après avoir atteint sa fin. (doit être fait en premier)
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); //Permet l'arrêt du thread à n'import qu'elle point de stop

	int ecoute_FD, esclave_FD, numero_page;	//Socket d'écoute et socket pour communiquer avec l'esclave accepter. Aussi le numéro de la page demandé par l'esclave
	void * addr_page; //Pointeur vers la page demandée par l'esclave, pour pouvoir l'envoyer

	struct sockaddr_in addrclt; //Structure pour démarrer un socket d'écoute.
	addrclt.sin_family = AF_INET; //-> IPv4.
	addrclt.sin_port = htons(0); //-> Port 0, port choisi par l'OS.
	addrclt.sin_addr.s_addr = INADDR_ANY; //-> Accepte toutes les adresse.

	//Ouverture du socket en TCP
	if((ecoute_FD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}

	//Lier le port au socket d'écoute, avec les paramètre de la structure que l'on a définie avant.
	if(bind(ecoute_FD, (struct sockaddr*)&addrclt, sizeof(addrclt)) == -1) {
		perror("bind");
		exit(2);
	}

	//Commencer à écouter sur le port.
	if(listen(ecoute_FD, 10) == -1) {
		perror("listen");
		exit(3);
	}

	//Boucle d'attende d'eclave pour leur passer la page demandée
	printf("-Thread esclave vers esclave initialiser\n\n");
	while(1){
		if((esclave_FD = accept(ecoute_FD, NULL, 0)) != -1) {
			//Reçois le numéro de la page demandé par l'esclave
			recv(esclave_FD, &numero_page, sizeof(int),0);

			//L'adresse de la page demandé est l'adresse de la zone mémoire + le nombre de page * la taille d'une page
			addr_page =  adresse + (numero_page * PAGE_SIZE);

			//Est-ce que le droit d'écriture est présent? Par jusqu'a être sur la donne entrée des info que l'on à.
			struct memoire_pages * temp_memoire = memoire;
			for(int i = numero_page; i != 0; i--){
				temp_memoire = temp_memoire->next;
			}

			//On vérifie dans notre mémoire info les droit, si on a pas les droit, on les met temporairement (éviter les défaut de page)
			pthread_mutex_lock(&mutex_esclave);//Lock le mutex pour pas que le programme principale change des droits/modifie pendant que l'on traite cette requête
			if(temp_memoire->droit_read == 0){
				mprotect(addr_page, PAGE_SIZE, PROT_READ);
			}
			
			//Envoie la page demandée
			send(esclave_FD, addr_page, PAGE_SIZE, 0);
			
			if(temp_memoire->droit_read == 0){
				mprotect(addr_page, PAGE_SIZE, PROT_NONE);
			}
			pthread_mutex_unlock(&mutex_esclave);
		}
	}
	//Ferme la reception d'autre envoie
	shutdown(esclave_FD, SHUT_RDWR);
	printf("-Fin esclave vers esclave\n");
	return NULL;
}

//Gére les défaut de page de l'esclave -> À vérifier
static void *handle_defaut(void * arg){
	pthread_detach(pthread_self()); //Détache le thread, pour qu'il se finise après avoir atteint sa fin. (doit être fait en premier)
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); //Permet l'arrêt du thread à n'import qu'elle point de stop

	struct handle_params p = *((struct handle_params *)arg); // Les argument passer en paramètre
	struct uffd_msg msg;	// Structure message de défaut de page, contient où et pourquoi
	struct pollfd pollfd;	// Structure de poll, qui est en faite le descripteur vers l'objet  userfaultfd que l'on à ouvert plus tôt
	int pollres; 			// Valeur pour vérifier le résultat du poll
	int msgres; 			// Valeur pour vérifier le résultat du message
	unsigned long addr_fault; // Adresse au début de la page où une faute à été commisse
	void * buf;	// Buffer pour récupérer une page

	int page_fault; //Numéro de la page qui fait défaut
	int req_demande_page; //Requête d'envoie et de reception
	struct uffdio_copy copy; // Structure permettant de remplacer la page

	struct sockaddr * addr_esclave = malloc(sizeof(struct sockaddr_in)); //Information d'un esclave
	int socket_esclave; //Socket de l'esclave

	pollfd.fd = p.uffd;
    pollfd.events = POLLIN|POLLOUT;	// les évènements attendus ; POLLIN et POLLOUT

    while(1) {
        // Attendre un événement userfaultfd(check toutes les 2 secondes)
        pollres = poll(&pollfd, 1, 2000);
        
		//Examination de l'événement demandé
        switch (pollres) {
		//-1 est une erreur
        case -1:
            perror("Erreur sur le poll\n");
            goto end;
        case 0:
            continue;
        case 1:
            break;
		//Cas questionnable.
        default:
            perror("Evenement inattendu(résultat poll)\n");
            goto end;
        }

		//Si l'événement à deja POLLERR dans le retour
        if (pollfd.revents & POLLERR) {
            perror("Erreur poll sur l'événement donnée\n");
            goto end;
        }

		//Après avoir confirmé que un événement est bien en cours, on lit le message userfaultfd pour savoir lequel c'est.
		//Vérifie pour erreur de lecture
        if ((msgres = read(p.uffd, &msg, sizeof(msg))) == -1) {
            perror("Erreur sur la lecture du message\n");
            goto end;
        }

		//Si le résultat n'est pas de la taille d'un message normale, il y a un problème
        if (msgres != sizeof(msg)) {
            perror("Taille du message non-conforme\n");
            goto end;
        }

        // Traitement des faute avec le type d'événement écrit dans le message -> seulement si l'evenement est un défaut de page(pas de UFFD_EVENT_FORK, UFFD_EVENT_REMAP, UFFD_EVENT_UNMAP)
        printf("Traitement de la faute de page:\n");

        if (msg.event & UFFD_EVENT_PAGEFAULT) {
			//Adresse de la page où la faute c'est produite
            addr_fault = msg.arg.pagefault.address;
            
            // Défaut d'écriture/lecture mais pas parce qu'il est protégé, juste car il manque la page
            if(((msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) && !(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP)) || (!(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) && !(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP))) {
                buf = malloc(PAGE_SIZE);
				/*printf("- Défaut de lecture/écriture\n");
				
				//On calcule le numéro de la page
				page_fault = (addr_fault - (long int)p.pointeurZoneMemoire) /PAGE_SIZE;
				req_demande_page = REQUETE_DEMANDE_PAGE;
				
				printf("- Page n°%d\n", page_fault);
				//Envoie de la demande de requête, puis le numéro de page
				if(send(maitre_fd, &req_demande_page, sizeof(int), 0)== -1){
					perror("send1");
					goto end;
				}
				if(send(maitre_fd, &page_fault, sizeof(int),0) == -1){
					perror("send2");
					goto end;
				}
				//Reçois la réponse du maître
				if(recv(maitre_fd, &req_demande_page, sizeof(int), 0)== -1){
					perror("recv1");
					goto end;
				}
				printf("- Réponse Maître: %d\n", req_demande_page);

				//Alloue le buffer car on va bientôt mettre une page dedans
				buf = malloc(PAGE_SIZE);

				//Si la réponse est de type RETOUR_DEMANDE_PAGE_ENVOIE, alors le maître nous envoie la page lui-même
				if(req_demande_page == RETOUR_DEMANDE_PAGE_ENVOIE){
					printf("- Récupère la page depuis le maître\n");
					//Reception de la page que l'on verse dans buf
					if(recv(maitre_fd, buf, PAGE_SIZE, 0)== -1){
						perror("recv2");
						goto end;
					}
				//Si la réponse est de type RETOUR_DEMANDE_PAGE_VERS_ESCLAVE, alors il est nécéssaire de demandé à un esclave 
				}else if(req_demande_page == RETOUR_DEMANDE_PAGE_VERS_ESCLAVE){
					printf("- Récupère la page depuis un autre eclave\n");
					
					//Reception des information de l'esclave 
					if(recv(maitre_fd, addr_esclave, sizeof(struct sockaddr_in), 0)== -1){
						perror("recv2");
						goto end;
					}
					
					printf("- Connexion Esclave\n");
					//Ouverture d'un socket TCP
					if((socket_esclave = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
						perror("Problème d'initialisation du socket\n");
						goto end;
					}

					//Tant que pas connecter à l'esclave on réessaye
					while(connect(socket_esclave, (struct sockaddr*)&addr_esclave, sizeof(addr_esclave)) == -1) {
						perror("Connection échouer");
						sleep(2);
					}

					printf("-Connecter\n");
					//On envoie le numéro de page à recevoir					
					if(send(socket_esclave, &page_fault, sizeof(int),0) == -1){
						perror("send3");
						goto end;
					}

					//Reception de la page que l'on verse dans buf
					if(recv(socket_esclave, buf, PAGE_SIZE, 0)== -1){
						perror("recv3");
						goto end;
					}

					//Ferme la connexion à l'esclave
					shutdown(socket_esclave, SHUT_RDWR);
				//Cas où le maître n'a pas su nous répondre
				}else{
					printf("- Pas de droit de lecture\n");
				}*/

                //Laisser l'utilisateur lire la page
				printf("- Pointeur début zone mémoire : %p\n- Pointeur page à régler : %p\n",p.pointeurZoneMemoire, (void *)addr_fault);

				//Copie le buffer dans la page qui fait défaut, réglant le problème
                copy.src = (unsigned long)buf;
                copy.dst = addr_fault;
                copy.len = PAGE_SIZE;
                copy.mode = 0;

				//Appel aux système pour remplacer cette page
                if (ioctl(p.uffd, UFFDIO_COPY, &copy) == -1) {
                    perror("Erreur durant la copie de la page\n");
                    goto end;
                }

				//Erreur si le retour "copy" n'est pas égale à length
				if(copy.copy != copy.len){
					perror("Taille copier pas normale\n");
					goto end;
				}

				//Libère le buffer utilisée
				free(buf);

				printf("- Défaut de page résolue\n");
			}//Sinon c'est une défaut de protection, pas gérer, cas inattendu.
			else {
                printf("- Page protégée : Pas gérer.\n");  
				//Erreur de segmentation
                raise(SIGSEGV);
			}
        }
	}

	end:
	//Désenregistrer la mémoire à surveiller
	free(addr_esclave);
	if (ioctl(p.uffd, UFFDIO_UNREGISTER, PAGE_SIZE * p.nombre_page)) {
        perror("Erreur pendant le désenregistrement du userfaultfd\n");
    }
	return NULL;
}

//Initialisation de l'esclave, créer le connexion et s'enregistre au maître, met en place le userfaultfd pour les fautes de pages, et l'écoute sur un port les autres esclaves. 
void* InitSlave(char* HostMaster) {
	int uffd;					// File descriptor pour l'object userfaultfd
	int connexion_FD;			// File descriptor de communication avec le maître (socket)
	int num_page;				// Nombre de pages allouée
	struct memoire_pages * temp_memoire; //Structure temporaire pour la création de la mémoire des pages
	struct addrinfo* res;		// Structure des info réseaux du maître récupérer
	struct in_addr ipv4;		// Structure IP du maître
	struct sockaddr_in addr; 	// Structure pour commencer la communication avec le maître
	unsigned long taille_recv;	// Taille de la mémoire partagée, reçu par le maître
	struct uffdio_api uffdio_api; //Structure de l'API de userfaultfd, vérifier la compabilité système
	struct uffdio_register uffdio_register; //Structure pour enregistrer la mémoire surveiller par userfaultfd
	struct handle_params handle_params;	// Paramètre à donner au thread de userfaultfd
	struct message req; //Initialisation de la connexion maître
	void * adresse; //Adresse du début de la zone mémoire

	printf("Initialisation Esclave:\n");
	/*----------------------------------MISE EN PLACE DE LA CONNECTION AU MAITRE--------------------------------------*/
	//Ouverture d'un socket TCP
	if((connexion_FD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("Problème d'initialisation du socket\n");
		exit(1);
	}

	printf("-Recherche Maitre\n");
	//Recherche de l'adresse IP du maître grâce à son nom.
	if(getaddrinfo(HostMaster, NULL, NULL, &res) != 0) {
		perror("Maître pas trouver!\n");
		exit(2);
	}

	//On prend juste l'adresse IP, c'est tout ce qu'on à besoin
	ipv4 = ((struct sockaddr_in*) res->ai_addr)->sin_addr;

	//On libère les info, elles ne sont plus nécéssaire
	freeaddrinfo(res);

	printf("-Connexion Maitre\n");
	//Mettre en place la structure pour communiquer avec le maître, on a deja un port prédéfinie
	memset((void*)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT_MAITRE);
	addr.sin_addr = ipv4;

	//On lance la connexion et tant que le maître ne répond pas, on attends.
	while(connect(connexion_FD, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("Connexion échouer\n");
		sleep(2);
	}

	// Enregistrer cette esclave au maitre et nous renvoie la taille de la mémoire partagée
	req.type = REQUETE_INIT;
	req.port = 0;

	if(send(connexion_FD, &req, sizeof(struct message), 0)== -1){
		perror("Problème communication 1\n");
	}
	if(recv(connexion_FD, &taille_recv, sizeof(long), 0) == -1){
		perror("Problème communication 2\n");
	}

	printf("-Taille reçu :%ld\n", taille_recv);

	/*----------------------------------MISE EN PLACE DE LA MÉMOIRE PARTAGER LOCALE--------------------------------------*/
	//Nouvelle zone mémoire de taille reçu par le maître avec aucun droit d'écriture/lecture, pour sa gestion.
	printf("-Ouverture mémoire partagé locale\n");
	if((adresse = mmap(NULL, taille_recv, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		perror("Problème initialisation de la mémoire");
		exit(2);
	}
	
	//Initialisation de variable globale utilisé par l'esclave
	maitre_fd = connexion_FD;
	region = adresse;
	printf("-Pointeur zone mémoire :%p\n", region);
	pthread_mutex_init(&mutex_esclave, NULL); //Mise en place du mutex
	
	//Calcule du nombre de page que l'on à alouer
	num_page = taille_recv / PAGE_SIZE;
    if((taille_recv % PAGE_SIZE) != 0){
		num_page++;
	}

	//Création de la structure memoire qui a pour but de ce rappeller des droits de lecture/ecriture sur les pages et de leur validitée
	memoire = malloc(sizeof(struct memoire_pages));

	temp_memoire = memoire;
	temp_memoire->droit_read = 0;
	temp_memoire->droit_write = 0;
	temp_memoire->validite = 0;
	temp_memoire->next = NULL;

	for(int temp = num_page-1; temp != 0; temp--){
		temp_memoire->next= malloc(sizeof(struct memoire_pages));
		temp_memoire->next->droit_read = 0;
		temp_memoire->next->droit_write = 0;
		temp_memoire->next->validite = 0;
		temp_memoire->next->next = NULL;
		temp_memoire = temp_memoire->next;
	}

	//Démarrer un thread pour les demande de pages depuis d'autres esclaves
	if(pthread_create(&pth_esclave, NULL, LoopSlave, (void *)adresse)!= 0) {
    	perror("Erreur de démarrage du thread 1\n");
    	exit(1);
    }

    /*----------------------------------MISE EN PLACE DE USERFAULTFD--------------------------------------*/    
	printf("- Mise en place du userfaultfd\n");

	// Créer et activer l'objet userfaultfd (nouvelle mise en place de userfaultfd)
    // -> _NR_userfaultfd : numéro de l'appelle système
    // -> O_CLOEXEC : (FLAG) Permet le multithreading
    // -> O_NONBLOCK : (FLAG) Ne pas bloquer durant POLL
    uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (uffd == -1) {
        perror("Problème de l'appelle système userfaultfd");
        exit(1);
    }

    // Activer la version de l'api et vérifier les fonctionnalités
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

    // Enregistrer la plage de mémoire du mappage que nous venons de créer pour qu'elle soit gérée par l'objet userfaultfd
    // UFFDIO_REGISTER_MODE_MISSING: l'espace utilisateur reçoit une notification de défaut de page en cas d'accès à une page manquante (les pages qui n'ont pas encore fait l'objet d'une faute)
    // UFFDIO_REGISTER_MODE_WP: l'espace utilisateur reçoit une notification de défaut de page lorsqu'une page protégée en écriture est écrite. Nécéssaire pour le handle de défaut de page
    //Début de la zone mémoire (pointeur)
    uffdio_register.range.start = (unsigned long)adresse;
    //Sa taille (nombre de page * taille d'une page) si ce n'est pas un multiple de page, UFFDIO_REGISTER ne marchera pas
    uffdio_register.range.len = num_page * PAGE_SIZE;
    uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP;

	printf("-Taille couverte par userfaultfd : %lld (nombre de page : %d)\n", uffdio_register.range.len,num_page);

    //Enregistre et setup notre userfaultfd avec la plage donnée (communication avec le noyau)
    if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		if(errno == EINVAL)
			perror("Un problème...");
		perror("ioctl/uffdio_register");
		exit(1);
	}

	//Démarrer le thread qui va gérer les défaut de pages (on lui donne des paramètres)
    handle_params.uffd = uffd;
	handle_params.nombre_page = num_page;
	handle_params.pointeurZoneMemoire = adresse;

	if(pthread_create(&pth_userfaultfd, NULL, handle_defaut, &handle_params) != 0) {
    	perror("Erreur de démarrage du thread 2");
    	exit(1);
    }

	return adresse;
}

// Trouver le numéro de page à partir d'une adresse
long trouver_numero_page(void* data) {
	return ((data - region)*sizeof(*data))/PAGE_SIZE;
}

//Demande le droit au maître de lire sur la/les page(s) où ce trouve le "adr" de taille s. (bloquant quand un écrivain est deja présent) -> A COMPLETER
void lock_read(void* addr, int size) {
	printf("\n--- LOCK_READ ---\n");

	// Requête envoyée au maître
	struct message message;
	message.type = REQUETE_LOCK_READ;
	message.debut_page = trouver_numero_page(addr);
	message.fin_page = trouver_numero_page(addr+size);

	printf("type : %d\ndebut : %d\nfin : %d\nsize : %d\n", message.type, message.debut_page,message.fin_page, size);

	//
	printf("Nombre de pages = %d\n", message.fin_page-message.debut_page+1);
	//

	//Demande de lock
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	// Page reçue du maître: restera bloquer sur le read
	// bloquant si un écrivain est actuellement sur la page
	printf("Pointeur1 : %p\n", addr);
	for(int i=message.debut_page ; i<=message.fin_page ; i++) { //Recevoir un où plusieurs pages
		
		void* data = addr - (addr-region) + PAGE_SIZE*i; //Pointeur vers la page reçu
		printf("Passage : %p\n", data);

		//Donner le droit de lecture sur la page
		if(mprotect(data, PAGE_SIZE, PROT_READ|PROT_WRITE) != 0){
			perror("mprotect1");
		}

		if(recv(maitre_fd, data, PAGE_SIZE, 0) == -1) {
			perror("read");
			exit(1);
		}

		if(mprotect(data, PAGE_SIZE, PROT_READ) != 0){
			perror("mprotect2");
		}

	}
	//printf("Pointeur2 : %p\nPointeur + size : %p\n", addr, addr+size);
	printf("Droits donnés\n");
}

//Rend le vérrou de la ou les pages en lecture -> A COMPLETER
void unlock_read(void* addr, int size) {
	printf("\n--- UNLOCK_READ ---\n");
	// Requête envoyée au maître
	struct message message;
	message.type = REQUETE_UNLOCK_READ;
	message.debut_page = trouver_numero_page(addr);
	message.fin_page = trouver_numero_page(addr+size);

	for(int i=message.debut_page ; i<=message.fin_page ; i++) { //Recevoir un où plusieurs pages
		void* data = addr - (addr-region) + PAGE_SIZE*i; //Pointeur vers la page reçu
		if(mprotect(data, PAGE_SIZE, PROT_NONE) != 0){
			perror("mprotect");
		}
	}

	//
	printf("nombre de pages = %d\n", message.fin_page-message.debut_page+1);
	//

	//Demande de unlock
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	//Attente que le maître finise
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("recv");
		exit(1);
	}
}

//Demande le droit au maître d'écrire sur la/les page(s) où ce trouve le "adr" de taille s. (bloquant quand un écrivain ou lecteur deja présent) -> A COMPLETER
void lock_write(void* addr, int size) {
	printf("--- LOCK_WRITE ---\n");

	// Requête envoyée au maître
	struct message message;
	message.type = REQUETE_LOCK_WRITE;
	message.debut_page = trouver_numero_page(addr);
	message.fin_page = trouver_numero_page(addr+size);

	//
	printf("nombre de pages = %d\n", message.fin_page-message.debut_page+1);
	//

	//Demande de lock
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	//Recevoir les pages à écrire
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		void* data = addr - (addr-region) + PAGE_SIZE*i;
		-
		if(mprotect(data, PAGE_SIZE, PROT_READ|PROT_WRITE)!= 0){
			perror("mprotect1");
		}

		if(recv(maitre_fd, data, PAGE_SIZE, 0) == -1) {
			perror("read");
			exit(1);
		}

		if(mprotect(data, PAGE_SIZE, PROT_WRITE) != 0){
			perror("mprotect2");
		}
	}	
}

//Rend le vérrou de la ou les pages en écriture -> A COMPLETER
void unlock_write(void* addr, int size) {
	printf("--- UNLOCK_WRITE ---\n");

	// Requête envoyée au maître
	struct message message;
	message.type = REQUETE_UNLOCK_WRITE;
	message.debut_page = trouver_numero_page(addr);
	message.fin_page = trouver_numero_page(addr+size);

	for(int i=message.debut_page ; i<=message.fin_page ; i++) { //Recevoir un où plusieurs pages
		void* data = addr - (addr-region) + PAGE_SIZE*i; //Pointeur vers la page reçu
		if(mprotect(data, PAGE_SIZE, PROT_NONE) != 0){
			perror("mprotect2");
		}
	}

	//
	printf("nombre de pages = %d\n", message.fin_page-message.debut_page+1);
	//

	//Demande de unlock
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	//Envoie des pages aux maître, celles qui on été écrite dans la zone addr + size 
	// -> faux, il est juste propriétaire
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		void* data = addr - (addr-region) + PAGE_SIZE*i;
		mprotect(data, PAGE_SIZE, PROT_READ);
		if(send(maitre_fd, data, PAGE_SIZE, 0) == -1) {
			perror("read");
			exit(1);
		}
		mprotect(data, PAGE_SIZE, PROT_NONE);
	}

	//Attente que le maître finise
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("recv");
		exit(1);
	}
}

//Rendre les pages aux maître et finalisée l'esclave -> A FINALISÉE 
void endSlave(void * data, int size){
	printf("\n--- FIN ESCLAVE ---\n");
	int nombre_page_rendre; //Contient le nombre de page que l'on va rendre aux maître
	int requete; //Requete envoyer/reçu
	struct memoire_pages * temp_memoire; //Structure temporaire pour la libération de la mémoire info
	
	//Arrête les thread.
	printf("Arrêt des thread\n");
    if (pthread_cancel(pth_esclave) != 0){
		perror("Erreur fin de thread 1");
		exit(1);
	}
	if (pthread_cancel(pth_userfaultfd) != 0){
		perror("Erreur fin de thread 1");
		exit(1);
	}

	//Demande de déclancher une fin au maître
	printf("Demande d'arrêt au maître\n");
	requete = REQUETE_FIN;
	if(send(maitre_fd, &requete, sizeof(int), 0)== -1){
		perror("Problème communication 1");
		exit(1);
	}

	//Le nombre de page à rendre au maître
	printf("Rendre les pages au maître\n");
	if(recv(maitre_fd, &nombre_page_rendre, sizeof(int), 0)== -1){
		perror("Problème communication 2");
		exit(1);
	}

	//Lui donnée les pages qui lui appartienne actuellement, une par une. -> A Compléter
	while(nombre_page_rendre != 0){
		//...........
		nombre_page_rendre--;
	}

	//Libère la mémoire partagé local
	printf("Libération de mémoire\n");
	if(munmap(data, size) == -1){
		perror("Erreur unmmap!");
	}

	//Libèrer la mémoire info
	temp_memoire = memoire;
	while(temp_memoire != NULL){
		memoire = temp_memoire->next;
		free(temp_memoire);
		temp_memoire = memoire;
	}
	printf("Fin Esclave\n");
}