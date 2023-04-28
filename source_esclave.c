/***** LIBRAIRIES *****/

#include <linux/userfaultfd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>


#include "lib.h"


/***** VARIABLES GLOBALES *****/

static struct memoire_pages * memoire;	// Vérifier les droits de l'esclave actuel sur chaque page et la validité de la page
static pthread_mutex_t mutex_esclave;	// Mutex permettant la cohérence dans le partage des données entre les différents threads
static pthread_t pth_esclave; 			// Thread qui permet, en cas d'invalidité d'une page, d'envoyer et recevoir la dernière version de celle-ci
static pthread_t pth_userfaultfd; 		// Thread qui gère les défauts de page (userfaultfd)
static int maitre_fd;					// Descripteur de fichier du maître
void *region;							// Début de la région gérée par userfaultfd

struct memoire_pages * trouver_mem_page(int numero){
	struct memoire_pages * temp_memoire = memoire;
	//printf("numero page dans %d\n", numero);
	for(int i = 0; i != numero; i++){
		//printf("passage\n");
		temp_memoire = temp_memoire->next;
	}
	return temp_memoire;
}

//Thread de communication esclave/esclave, permet d'envoyer les page qu'il est propriétaire (prend l'adresse de la mémoire partagée en paramètre)
void* LoopSlave(void * port){
	pthread_detach(pthread_self()); //Détache le thread, pour qu'il se finise après avoir atteint sa fin. (doit être fait en premier)
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); //Permet l'arrêt du thread à n'import qu'elle point de stop

	struct message msg; //Message reçu : maitre dissant l'invadibilité ou l'esclave demande une page
	int ecoute_FD, esclave_FD;	//Socket d'écoute et socket pour communiquer avec l'esclave accepter. Aussi le numéro de la page demandé par l'esclave
	void * addr_page; //Pointeur vers la page demandée par l'esclave, pour pouvoir l'envoyer

	struct sockaddr_in addrclt; //Structure pour démarrer un socket d'écoute.
	struct memoire_pages * temp_memoire;
	addrclt.sin_family = AF_INET; //-> IPv4.
	addrclt.sin_port = *((int *)port); //-> Port alléatoire, entre 1024 et 5000.
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
			printf("Connexion depuis l'exterieur\n");
			//Reçois le numéro de la page demandé par l'esclave
			msg.type = -1;
			//Attende de la reception d'une rêquete
			if(recv(esclave_FD, &msg, sizeof(struct message),0)== -1){
				perror("Problème communication 1");
			}

			switch (msg.type){
			case REQUETE_INVALIDE_PAGE:
				printf("INVALIDATION DE LA PAGE %d\n", msg.debut_page);
				//pthread_mutex_lock(&mutex_esclave);//Lock le mutex pour pas que le programme principale change des droits/modifie pendant que l'on traite cette requête
				temp_memoire = trouver_mem_page(msg.debut_page);
				temp_memoire->validite = 0;

				msg.type = ACK;
				if(send(esclave_FD, &msg, sizeof(struct message), 0) == -1) {
					perror("send");
					exit(1);
				}
				
				printf("ACK SENT\n");

				if(shutdown(esclave_FD, SHUT_RDWR) == -1){
					perror("shutdown");
					exit(1);
				}		

				if(close(esclave_FD) == -1){
					perror("shutdown");
					exit(1);
				}

				printf("FIN\n");

				//pthread_mutex_unlock(&mutex_esclave);
				break;
			case REQUETE_DEMANDE_PAGE:
				printf("DEMANDE DE LA PAGE %d\n", msg.debut_page);
				//L'adresse de la page demandé est l'adresse de la zone mémoire + le nombre de page * la taille d'une page
				//pthread_mutex_lock(&mutex_esclave);//Lock le mutex pour pas que le programme principale change des droits/modifie pendant que l'on traite cette requête
				addr_page =  region + (msg.debut_page * PAGE_SIZE);

				//Est-ce que le droit d'écriture est présent? Par jusqu'a être sur la donne entrée des info que l'on à.
				temp_memoire = trouver_mem_page(msg.debut_page);

				//On vérifie dans notre mémoire info les droit, si on a pas les droit, on les met temporairement (éviter les défaut de page)
				if(temp_memoire->droit_read == 0){
					if(mprotect(addr_page, PAGE_SIZE, PROT_READ) != 0){
						perror("mprotect 1");
					}
				}
			
				//Envoie la page demandée
				if(send(esclave_FD, addr_page, PAGE_SIZE, 0)== -1){
					perror("Problème communication 2");
				}
			
				if(temp_memoire->droit_read == 0){
					if(mprotect(addr_page, PAGE_SIZE, PROT_NONE)!= 0){
						perror("mprotect 2");
					}
				}

				if(shutdown(esclave_FD, SHUT_RDWR) == -1){
					perror("shutdown");
					exit(1);
				}		

				if(close(esclave_FD) == -1){
					perror("shutdown");
					exit(1);
				}

				printf("FIN\n");

				//pthread_mutex_unlock(&mutex_esclave);
				break;
			default:
				printf("Message fauté : %d\n", msg.type);
				break;
			}
		}
	}
	//Ferme la reception d'autre envoie
	if(shutdown(ecoute_FD, SHUT_RDWR) == -1){
		perror("shutdown");
		exit(1);
	}		
	
	if(close(ecoute_FD)){
		perror("shutdown");
		exit(1);
	}

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

	struct uffdio_zeropage zero; // Structure permettant de remplacer la page


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
                //Laisser l'utilisateur lire la page
				printf("- Pointeur début zone mémoire : %p\n- Pointeur page à régler : %p\n",p.pointeurZoneMemoire, (void *)addr_fault);

				//Met en place la structure pour remplir la page de 0, réglant le problème
                zero.range.len = PAGE_SIZE;
				zero.range.start = addr_fault;
				zero.mode = 0;  

				//Appel aux système pour remplacer cette page
                if (ioctl(p.uffd, UFFDIO_ZEROPAGE, &zero) == -1) {
                    perror("Erreur durant la copie de la page\n");
                    goto end;
                }

				//Erreur si le retour "copy" n'est pas égale à length
				if(zero.zeropage != PAGE_SIZE){
					perror("Taille copier pas normale\n");
					goto end;
				}

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
	int port;					// Port écoute de l'esclave
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
	srand(time(NULL));
	port = 1024 + rand() % (5000 + 1 - 1024);
	req.type = REQUETE_INIT;
	req.port = port;

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
	temp_memoire->num_page = 0;
	temp_memoire->next = NULL;

	for(int temp = 0; temp != num_page-1; temp++){
		temp_memoire->next= malloc(sizeof(struct memoire_pages));
		temp_memoire->next->droit_read = 0;
		temp_memoire->next->droit_write = 0;
		temp_memoire->next->validite = 0;
		temp_memoire->next->num_page = temp+1;
		temp_memoire->next->next = NULL;
		temp_memoire = temp_memoire->next;
	}



	//Démarrer un thread pour les demande de pages depuis d'autres esclaves
	if(pthread_create(&pth_esclave, NULL, LoopSlave, (void *)&port)!= 0) {
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
	uffdio_api.features = 0;

    //IOCTL : permet de communiquer entre le mode user et kernel. On demande une requête de type UFFDIO_API qui va remplir notre structure uffdio_api
    if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
        perror("ioctl/uffdio_api");
        exit(1);
    }

    //Vérifier que l'API récupérer est la bonne, que notre système est compatible avec userfaultfd API
    if (uffdio_api.api != UFFD_API) {
        fprintf(stderr, "unsupported userfaultfd api (1)\n");
        exit(1);
    }
	/*else if (!(uffdio_api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP)) {
    	fprintf(stderr, "unsupported userfaultfd api (3)\n");
    	exit(1);
    }*/

    // Enregistrer la plage de mémoire du mappage que nous venons de créer pour qu'elle soit gérée par l'objet userfaultfd
    // UFFDIO_REGISTER_MODE_MISSING: l'espace utilisateur reçoit une notification de défaut de page en cas d'accès à une page manquante (les pages qui n'ont pas encore fait l'objet d'une faute)
    // UFFDIO_REGISTER_MODE_WP: l'espace utilisateur reçoit une notification de défaut de page lorsqu'une page protégée en écriture est écrite. Nécéssaire pour le handle de défaut de page
    //Début de la zone mémoire (pointeur)
    uffdio_register.range.start = (unsigned long)adresse;
    //Sa taille (nombre de page * taille d'une page) si ce n'est pas un multiple de page, UFFDIO_REGISTER ne marchera pas
    uffdio_register.range.len = num_page * PAGE_SIZE;
    uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;

	printf("-Taille couverte par userfaultfd : %lld (nombre de page : %d)\n", uffdio_register.range.len,num_page);

    //Enregistre et setup notre userfaultfd avec la plage donnée (communication avec le noyau)
    if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
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
	
	sleep(1);
	return adresse;
}

// Trouver le numéro de page à partir d'une adresse
long trouver_numero_page(void* data) {
	return ((data - region)*sizeof(*data))/PAGE_SIZE;
}

//Demande le droit au maître de lire sur la/les page(s) où ce trouve le "adr" de taille s. (bloquant quand un écrivain est deja présent) -> A COMPLETER
void lock_read(void* addr, int size) {
	printf("\n--- LOCK_READ ---\n");
	pthread_mutex_lock(&mutex_esclave);

	// Requête envoyée au maître
	int socket_esclave;
	int debut = trouver_numero_page(addr);
	int fin = trouver_numero_page(addr+size);
	struct message message;
	struct memoire_pages * temp_memoire;
	message.type = REQUETE_LOCK_READ;
	message.debut_page = debut;
	message.fin_page = fin;

	//printf("type : %d\ndebut : %d\nfin : %d\nsize : %d\n", message.type, message.debut_page,message.fin_page, size);

	//
	//printf("Nombre de pages = %d\n", message.fin_page-message.debut_page+1);
	//

	//Demande de lock
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	printf("Attente écrivain...\n");
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("read");
		exit(1);
	}
	printf("Fin attente\n");

	if(message.type != ACK){
		printf("Problème synchronisation!!%d\n",message.type);
		exit(1);
	}

	// Page reçue du maître: restera bloquer sur le read
	// bloquant si un écrivain est actuellement sur la page
	for(int i=debut ; i<=fin ; i++) { //Recevoir un où plusieurs pages
		printf("PAGE NUMERO : %d\n", i);
		temp_memoire = trouver_mem_page(i);
		void* data = addr - (addr-region) + PAGE_SIZE*i; //Pointeur vers la page reçu
		if(temp_memoire->droit_read == 0 || temp_memoire->validite == 0){
			printf("(Debug : Page invalide? %d)\n", temp_memoire->validite);

			printf("Donne droit d'écrire pour récupérer la page\n");
			//Donner le droit de lecture sur la page
			if(mprotect(data, PAGE_SIZE, PROT_WRITE|PROT_READ) != 0){
				perror("mprotect1");
			}

			if(temp_memoire->validite == 0){
				printf("Reçois le moyen de récupérer la page\n");
				if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
					perror("read");
					exit(1); 
				}


				if(message.type == RETOUR_DEMANDE_PAGE_ENVOIE){
					printf("Recupération depuis le maître\n");
					if(recv(maitre_fd, data, PAGE_SIZE, 0) == -1) {
						perror("read");
						exit(1);
					}
				}else if(message.type == RETOUR_DEMANDE_PAGE_VERS_ESCLAVE){
					printf("Recupération depuis l'esclave\n");
					//Ouverture d'un socket TCP
					if((socket_esclave = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
						perror("Problème d'initialisation du socket\n");
						exit(1);
					}

					printf("Connexion\n");
					//Tant que pas connecter à l'esclave on réessaye
					while(connect(socket_esclave, (struct sockaddr*)&message.esclave_info, sizeof(message.esclave_info)) == -1) {
						perror("Connection échouer");
						sleep(2);
					}

					//On envoie le numéro de page à recevoir	
					printf("Envoie demande page\n");
					message.type = REQUETE_DEMANDE_PAGE;
					message.debut_page = i;			
					if(send(socket_esclave, &message, sizeof(struct message),0) == -1){
						perror("send3");
						exit(1);
					}

					printf("Reception de page\n");
					//Reception de la page que l'on verse dans buf
					if(recv(socket_esclave, data, PAGE_SIZE, 0)== -1){
						perror("recv3");
						exit(1);
					}

					printf("Page reçu\n");

					//Ferme la connexion à l'esclave
					if(shutdown(socket_esclave, SHUT_RDWR) == -1){
						perror("shutdown");
					}
				
					if(close(socket_esclave)){
						perror("shutdown");
						exit(1);
					}		

					printf("Envoie du ACK au maître pour confirmée cette page\n");
					//Confirme au maître que tout c'est bien passer
					message.type = ACK;
					if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
						perror("write");
						exit(1);
					}
				}else{
					printf("pas reçu :< %d", message.type);
					exit(1);
				}
				temp_memoire->validite = 1;
			}

			printf("Met les droit de lecture uniquement sur la page\n");
			if(mprotect(data, PAGE_SIZE, PROT_READ) != 0){
				perror("mprotect2");
			}
			temp_memoire->droit_read = 1;
		}

		printf("ACK final\n");
		if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
			perror("read");
			exit(1);
		}

		if(message.type != ACK){
			printf("%d\n",message.type);
			perror("synch");
			exit(1);
		}
	}
	//printf("Pointeur2 : %p\nPointeur + size : %p\n", addr, addr+size);
	pthread_mutex_unlock(&mutex_esclave);
	printf("Droits donnés\n");
}

//Rend le vérrou de la ou les pages en lecture -> A COMPLETER
void unlock_read(void* addr, int size) {
	printf("\n--- UNLOCK_READ ---\n");
	pthread_mutex_lock(&mutex_esclave);

	// Requête envoyée au maître
	struct message message;
	struct memoire_pages * temp_memoire;
	message.type = REQUETE_UNLOCK_READ;
	message.debut_page = trouver_numero_page(addr);
	message.fin_page = trouver_numero_page(addr+size);

	for(int i=message.debut_page ; i<=message.fin_page ; i++) { //Recevoir un où plusieurs pages
		void* data = addr - (addr-region) + PAGE_SIZE*i; //Pointeur vers la page reçu
		temp_memoire = trouver_mem_page(i);
		if(mprotect(data, PAGE_SIZE, PROT_NONE) != 0){
			perror("mprotect");
		}
		temp_memoire->droit_read = 0;
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

	if(message.type != ACK){
		printf("Problème synchronisation!!%d\n",message.type);
		exit(1);
	}

	pthread_mutex_unlock(&mutex_esclave);
}

//Demande le droit au maître d'écrire sur la/les page(s) où ce trouve le "adr" de taille s. (bloquant quand un écrivain ou lecteur deja présent) -> A COMPLETER
void lock_write(void* addr, int size) {
	printf("--- LOCK_WRITE ---\n");
	pthread_mutex_lock(&mutex_esclave);

	// Requête envoyée au maître
	struct message message;
	struct memoire_pages * temp_memoire;
	int debut = trouver_numero_page(addr); 
	int fin = trouver_numero_page(addr+size);
	int socket_esclave = 0;
	message.type = REQUETE_LOCK_WRITE;
	message.debut_page = debut;
	message.fin_page = fin;

	//
	printf("nombre de pages = %d\n", message.fin_page-message.debut_page+1);
	//

	printf("type : %d\ndebut : %d\nfin : %d\nsize : %d\n", message.type, message.debut_page,message.fin_page, size);

	//Demande de lock
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	printf("Attente écrivain...\n");
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("read");
		exit(1);
	}
	printf("Fin attente\n");

	if(message.type != ACK){
		printf("Problème synchronisation!!%d\n",message.type);
		exit(1);
	}

	//Recevoir les pages à écrire
	for(int i=debut ; i<=fin ; i++) {
		temp_memoire = trouver_mem_page(i);
		void* data = addr - (addr-region) + PAGE_SIZE*i;
		if(temp_memoire->droit_write == 0 || temp_memoire->validite == 0){
			
			if(mprotect(data, PAGE_SIZE, PROT_READ|PROT_WRITE)!= 0){
				perror("mprotect1");
			}

			if(temp_memoire->validite == 0){
				if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
					perror("read");
					exit(1);
				}

				if(message.type == RETOUR_DEMANDE_PAGE_ENVOIE){
					printf("Recupération depuis le maître\n");
					if(recv(maitre_fd, data, PAGE_SIZE, 0) == -1) {
						perror("read");
						exit(1);
					}
					printf("- Récupérer\n");
				}else if(message.type == RETOUR_DEMANDE_PAGE_VERS_ESCLAVE){
					printf("Recupération depuis l'esclave\n");
					//Ouverture d'un socket TCP
					if((socket_esclave = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
						perror("Problème d'initialisation du socket\n");
						exit(1);
					}

					//Tant que pas connecter à l'esclave on réessaye
					while(connect(socket_esclave, (struct sockaddr*)&message.esclave_info, sizeof(message.esclave_info)) == -1) {
						perror("Connection échouer");
						sleep(2);
					}

					//On envoie le numéro de page à recevoir		
					message.type = REQUETE_DEMANDE_PAGE;
					message.debut_page = i;			
					if(send(socket_esclave, &message, sizeof(struct message),0) == -1){
						perror("send3");
						exit(1);
					}

					//Reception de la page que l'on verse dans buf
					if(recv(socket_esclave, data, PAGE_SIZE, 0)== -1){
						perror("recv3");
						exit(1);
					}

					//Ferme la connexion à l'esclave
					if(shutdown(socket_esclave, SHUT_RDWR) == -1){
						perror("shutdown");
					}
				
					if(close(socket_esclave)){
						perror("shutdown");
						exit(1);
					}

					//Confirme au maître que tout c'est bien passer
					message.type = ACK;
					if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
						perror("write");
						exit(1);
					}
				}
			}

			temp_memoire->validite = 1;
			if(mprotect(data, PAGE_SIZE, PROT_WRITE) != 0){
				perror("mprotect2");
			}
			temp_memoire->droit_write = 1;
		}

		if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
			perror("write");
			exit(1);
		}

		if(message.type != ACK){
			printf("%d\n",message.type);
			perror("synch");
			exit(1);
		}
		printf("- Passage\n");
	}	
	printf("--- FIN LOCK_WRITE ---\n");
	pthread_mutex_unlock(&mutex_esclave);
}

//Rend le vérrou de la ou les pages en écriture -> A COMPLETER
void unlock_write(void* addr, int size) {
	printf("--- UNLOCK_WRITE ---\n");
	pthread_mutex_lock(&mutex_esclave);

	// Requête envoyée au maître
	struct message message;
	struct memoire_pages * temp_memoire;
	message.type = REQUETE_UNLOCK_WRITE;
	message.debut_page = trouver_numero_page(addr);
	message.fin_page = trouver_numero_page(addr+size);

	for(int i=message.debut_page ; i<=message.fin_page ; i++) { //Recevoir un où plusieurs pages
		void* data = addr - (addr-region) + PAGE_SIZE*i; //Pointeur vers la page reçu
		temp_memoire = trouver_mem_page(i);
		temp_memoire->droit_write = 0;
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

	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		void* data = addr - (addr-region) + PAGE_SIZE*i;
		if(mprotect(data, PAGE_SIZE, PROT_NONE)!= 0){
			perror("mprotect2");
		}
	}

	//Attente que le maître finise
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("recv");
		exit(1);
	}

	if(message.type != ACK){
		printf("synch unlock write : %d", message.type);
		exit(1);
	}

	pthread_mutex_unlock(&mutex_esclave);
	printf("--- FIN UNLOCK_WRITE ---\n");
}

//Rendre les pages aux maître et finalisée l'esclave -> A FINALISÉE 
void endSlave(void * data, int size){
	printf("\n--- FIN ESCLAVE ---\n");
	int nombre_page_rendre; //Contient le nombre de page que l'on va rendre aux maître
	int nombre_page;
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
		if(recv(maitre_fd, &nombre_page, sizeof(int), 0)== -1){
			perror("Problème communication 3");
			exit(1);
		}
		printf("Rendre page %d\n", nombre_page);

		void* data = region + PAGE_SIZE*nombre_page; //Pointeur vers la page reçu
		
		if(mprotect(data, PAGE_SIZE, PROT_READ) != 0){
			perror("mprotect2");
			exit(1);
		}

		if(send(maitre_fd, data, PAGE_SIZE, 0)== -1){
			perror("Problème communication 4");
			exit(1);
		}

		if(mprotect(data, PAGE_SIZE, PROT_NONE) != 0){
			perror("mprotect2");
			exit(1);
		}
		nombre_page_rendre--;
	}

	//Attente que le maître finise
	struct message message;
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("recv");
		exit(1);
	}

	if(message.type != ACK){
		printf("synch unlock write : %d", message.type);
		exit(1);
	}

	if(shutdown(maitre_fd, SHUT_RDWR) == -1){
		perror("shutdown");
		exit(1);
	}

	if(close(maitre_fd) == -1){
		perror("shutdown");
		exit(1);
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





/*ANCIENT CODE USERFAULTFD
printf("- Défaut de lecture/écriture\n");
				
				//On calcule le numéro de la page
				page_fault = (addr_fault - (long int)p.pointeurZoneMemoire) /PAGE_SIZE;
				req_demande_page = REQUETE_DEMANDE_PAGE;
				
				printf("- Page n°%d\n", page_fault);
				//Envoie de la demande de requête, puis le numéro de page
				if(send(maitre_fd, &req_demande_page, sizeof(int), 0)== -1){
					perror("send1");
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