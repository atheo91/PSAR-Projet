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

/**
 * Renvoie les droits de l'esclave et la validité de la
 * version qu'a actuellement l'esclave de la page donnée
 */
struct memoire_pages * trouver_mem_page(int numero){
	struct memoire_pages * temp_memoire = memoire;
	for(int i = 0; i != numero; i++){
		temp_memoire = temp_memoire->next;
	}
	return temp_memoire;
}

/**
 * Thread de communication entre esclaves petmettant
 * d'envoyer les pages dont il est propriétaire aux
 * esclaves demandeurs
 */
void* LoopSlave(void * port){
	// Détache le thread afin qu'il puisse se terminer
	pthread_detach(pthread_self());
	// Permet l'arrêt du thread à n'importe quel point de stop
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	struct message msg;						// Message reçu : maitre disant l'invadibilité ou l'esclave demande une page
	int ecoute_FD, esclave_FD;				// Sockets d'écoute et de communication avec l'esclave accepté
	void* addr_page; 						// Pointeur vers la page demandée par l'esclave, pour pouvoir l'envoyer
	struct memoire_pages * temp_memoire;	// Droits d'accès aux pages

	struct sockaddr_in addrclt; 			// Structure pour démarrer un socket d'écoute
	addrclt.sin_family = AF_INET; 			// IPv4
	addrclt.sin_port = *((int *)port); 		// Port aléatoire entre 1024 et 5000
	addrclt.sin_addr.s_addr = INADDR_ANY; 	// Accepter toutes les adresses

	// Ouverture du socket en TCP
	if((ecoute_FD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("[ERROR][LoopSlave] socket");
		exit(1);
	}

	// Lier le port au socket d'écoute, avec les paramètres de la structure que l'on a définie avant
	if(bind(ecoute_FD, (struct sockaddr*)&addrclt, sizeof(addrclt)) == -1) {
		perror("[ERROR][LoopSlave] bind");
		exit(2);
	}

	// Commencer à écouter sur le port
	if(listen(ecoute_FD, 10) == -1) {
		perror("[ERROR][LoopSlave] listen");
		exit(3);
	}

	// Boucle d'attende d'eclaves pour leur donner la page demandée
	while(1) {
		if((esclave_FD = accept(ecoute_FD, NULL, 0)) != -1) {
			// Recevoir le numéro de la page demandée par l'esclave
			msg.type = -1;
			// Attendre la réception d'un message
			if(recv(esclave_FD, &msg, sizeof(struct message), 0) == -1){
				perror("[ERROR][LoopSlave] recv (1)");
			}

			// Effectuer une action en fonction du type de message reçu
			switch (msg.type){

			// Les pages qu'on possède ont été invalidées par un autre esclave
			case REQUETE_INVALIDE_PAGE:
				printf("[INVAL] Demande d'invalidition d'une page: P%d\n", msg.debut_page);

				temp_memoire = trouver_mem_page(msg.debut_page);
				temp_memoire->validite = 0;

				// Message d'acquittement à envoyer
				msg.type = ACK;
				if(send(esclave_FD, &msg, sizeof(struct message), 0) == -1) {
					perror("[ERROR][LoopSlave] send (1)");
					exit(1);
				}

				// Fermer la connexion
				if(shutdown(esclave_FD, SHUT_RDWR) == -1){
					perror("[ERROR][LoopSlave] shutdown (1)");
					exit(1);
				}		

				if(close(esclave_FD) == -1){
					perror("[ERROR][LoopSlave] close (1)");
					exit(1);
				}
				break;

			// Un esclave nous demande nos pages
			case REQUETE_DEMANDE_PAGE:
				printf("[ENVOI] Demande de récupération d'une page: envoi de P%d à M%d\n", msg.debut_page, esclave_FD);

				// Adresse de la page demandée
				addr_page =  region + (msg.debut_page * PAGE_SIZE);

				temp_memoire = trouver_mem_page(msg.debut_page);

				// Vérifier le droit de lecture sur la page, si on n'a pas ce droit, on le met temporairement afin d'éviter les défauts de page
				if(temp_memoire->droit_read == 0) {
					if(mprotect(addr_page, PAGE_SIZE, PROT_READ) != 0){
						perror("[ERROR][LoopSlave] mprotect (1)");
						exit(1);
					}
				}
			
				// Envoi la page demandée à l'esclave demandeur
				if(send(esclave_FD, addr_page, PAGE_SIZE, 0)== -1){
					perror("[ERROR][LoopSlave] send (2)");
					exit(1);
				}
			
				// Retirer les droits s'il ne les possédait pas auparavant
				if(temp_memoire->droit_read == 0){
					if(mprotect(addr_page, PAGE_SIZE, PROT_NONE)!= 0){
						perror("[ERROR][LoopSlave] mprotect (2)");
						exit(1);
					}
				}

				// Fermeture de la connexion
				if(shutdown(esclave_FD, SHUT_RDWR) == -1){
					perror("[ERROR][LoopSlave] shutdown (2)");
					exit(1);
				}		

				if(close(esclave_FD) == -1){
					perror("[ERROR][LoopSlave] close (2)");
					exit(1);
				}
				break;

			default:
				printf("[ERROR] Réception d'un message erroné ! (n°%d)\n", msg.type);
				break;
			}
		}
	}

	// Fermeture de la connexion
	if(shutdown(ecoute_FD, SHUT_RDWR) == -1){
		perror("[ERROR][LoopSlave] shutdown (3)");
		exit(1);
	}		
	
	if(close(ecoute_FD)){
		perror("[ERROR][LoopSlave] close (3)");
		exit(1);
	}

	free(port);

	return NULL;
}

/**
 * Gère les défauts de page de l'esclave
 */
static void* handle_defaut(void* arg){
	// Détache le thread afin qu'il puisse se terminer
	pthread_detach(pthread_self());
	// Permet l'arrêt du thread à n'importe quel point de stop
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	struct handle_params p;			// Récupérer en forme de structures les arguments donnés en paramètre au thread
	struct uffd_msg msg;			// Structure message de défaut de page, contient où se trouve le défaut de page et comment il a été provoqué
	struct pollfd pollfd;			// Structure de poll, qui est le descripteur vers l'objet  userfaultfd que l'on a ouvert plus tôt
	int pollres; 					// Valeur pour vérifier le résultat du poll
	int msgres; 					// Valeur pour vérifier le résultat du message
	unsigned long addr_fault; 		// Adresse au début de la page où une faute à été commise
	struct uffdio_zeropage zero; 	// Structure permettant le remplacement de la page

	p = *((struct handle_params *)arg);

	pollfd.fd = p.uffd;
    pollfd.events = POLLIN|POLLOUT;	// les évènements attendus sont POLLIN et POLLOUT

    while(1) {
        // Attendre un événement userfaultfd (vérification toutes les 2 secondes)
        pollres = poll(&pollfd, 1, 2000);
        
		// Examination de l'évènement demandé
        switch (pollres) {
        case -1:
            perror("[ERROR][handle_defaut] poll (1)");
            goto end;
        case 0:
            continue;
        case 1:
            break;
		// Cas questionnable
        default:
        	perror("[ERROR][handle_defaut] poll (2)");
            goto end;
        }

		// Si l'événement a une erreur dans le retour
        if (pollfd.revents & POLLERR) {
        	perror("[ERROR][handle_defaut] poll (3)");
            goto end;
        }

		// Après avoir confirmé qu'un événement est bien en cours, on lit le message les informations sur le défaut de page
        if ((msgres = read(p.uffd, &msg, sizeof(msg))) == -1) {
        	perror("[ERROR][handle_defaut] read (1)");
            goto end;
        }

		// Si le résultat n'est pas de la taille du message
        if (msgres != sizeof(msg)) {
        	perror("[ERROR][handle_defaut] Taille du message non-conforme");
            goto end;
        }

        // Traitement des défauts de page avec le type d'événement écrit dans le message
        if (msg.event & UFFD_EVENT_PAGEFAULT) {
			// Adresse de la page où la faute s'est produite
            addr_fault = msg.arg.pagefault.address;
            
            // Défaut d'écriture/lecture mais pas parce qu'il est protégé, juste car il manque la page
            if(((msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) && !(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP))
            		|| (!(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WRITE) && !(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP))) {

				// Mettre en place la structure pour remplir la page de 0, réglant le problème
                zero.range.len = PAGE_SIZE;
				zero.range.start = addr_fault;
				zero.mode = 0;  

				// Appel aux systèmes pour remplacer cette page
                if (ioctl(p.uffd, UFFDIO_ZEROPAGE, &zero) == -1) {
                	perror("[ERROR][handle_defaut] ioctl");
                    goto end;
                }

				// Erreur si la zone copiée n'est pas conforme
				if(zero.zeropage != PAGE_SIZE){
					perror("[ERROR][handle_defaut] Taille de la zone copiée non-conforme");
					goto end;
				}

				printf("[INFOS] Défaut de page résolu !\n");
			}
            // Si c'est une défaut de protection, cas inattendu
			else {
				printf("[ERROR] Page protégée !\n");
				// Erreur de segmentation
                raise(SIGSEGV);
			}
        }
	}

	end:
	// Désenregistrer la mémoire à surveiller
	if (ioctl(p.uffd, UFFDIO_UNREGISTER, PAGE_SIZE * p.nombre_page)) {
        perror("[ERROR][handle_defaut] ioctl (2)");
        exit(1);
    }

	free(arg);

	return NULL;
}

/**
 * Etablir une connexion avec le maître
 */
void set_connection(char* HostMaster, int* port, long* taille_recv) {
	printf("[~] Tentative de connexion avec le maître\n");

	struct message req; 		// Message de connexion avec le maître
	struct addrinfo* res;		// Structure des infos réseau du maître récupérés
	struct in_addr ipv4;		// Structure IP du maître
	struct sockaddr_in addr; 	// Structure pour commencer la communication avec le maître

	// Ouverture d'un socket TCP
	if((maitre_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("[ERROR][set_connection] socket");
		exit(1);
	}

	// Recherche de l'adresse IP du maître grâce à son nom
	if(getaddrinfo(HostMaster, NULL, NULL, &res) != 0) {
		perror("[ERROR][set_connection] getaddrinfo");
		exit(2);
	}

	// Récupérer l'adresse IP
	memset((void*)&addr, 0, sizeof(addr));
	addr.sin_family = ((struct sockaddr_in*) res->ai_addr)->sin_family;
	addr.sin_port = htons(PORT_MAITRE);
	addr.sin_addr = ((struct sockaddr_in*) res->ai_addr)->sin_addr;

	// Libèrer les infos
	freeaddrinfo(res);

	// Mettre en place la structure pour communiquer avec le maître
	

	// Lancement de la connexion: tant que le maître ne répond pas, attendre...
	while(connect(maitre_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		printf("[~] En attente de connexion avec le maître...\n");
		sleep(2);
	}

	// Enregistrer cet esclave au maitre et nous renvoie la taille de la mémoire partagée
	srand(time(NULL));
	*port = 1024 + rand() % (5000 + 1 - 1024);
	req.type = REQUETE_INIT;
	req.port = *port;

	if(send(maitre_fd, &req, sizeof(struct message), 0)== -1){
		perror("[ERROR][set_connection] send (1)");
		exit(1);
	}

	// Réception de la taille de la mémoire partagée
	if(recv(maitre_fd, taille_recv, sizeof(long), 0) == -1){
		perror("[ERROR][set_connection] send (2)");
		exit(1);
	}

	printf("[OK] Connexion établie avec le maître\n");
}

/**
 * Mise en place du gestionnaire de défauts de page
 */
void set_userfaultfd(int num_page) {
	printf("[~] Tentative de mise en place du userfaultfd\n");

	int uffd;								// Descripteur de fichier pour l'objet userfaultfd
	struct uffdio_api uffdio_api; 			// Structure de l'API de userfaultfd, vérifier la compabilité système
	struct uffdio_register uffdio_register;	// Structure pour enregistrer la mémoire surveillée par userfaultfd
	struct handle_params* handle_params;	// Paramètre à donner au thread de userfaultfd

	handle_params = malloc(sizeof(struct handle_params));

	// Créer et activer l'objet userfaultfd (nouvelle mise en place de userfaultfd)
	// _NR_userfaultfd 	: Numéro de l'appel système
	// O_CLOEXEC 		: Flag permettant le multithreading
	// O_NONBLOCK 		: Flag permettant de ne pas bloquer durant POLL
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1) {
		perror("[ERROR][set_userfaultfd] syscall");
		exit(1);
	}

	// Activer la version de l'api et vérifier les fonctionnalitées
	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;

	// IOCTL: permet la communication entre le 'mode user' et 'kernel'
	// On demande une requête de type UFFDIO_API qui va remplir notre structure uffdio_api
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
		perror("[ERROR][set_userfaultfd] ioctl/uffdio_api");
		exit(1);
	}

	// Vérifier que l'API récupérée est supportée par notre système et est compatible avec userfaultfd API
	if (uffdio_api.api != UFFD_API) {
		fprintf(stderr, "[ERROR][set_userfaultfd] unsupported userfaultfd api (1)\n");
		exit(1);
	}

	// Enregistrer la plage de mémoire du mappage que nous venons de créer pour qu'elle soit gérée par l'objet userfaultfd
	// UFFDIO_REGISTER_MODE_MISSING: l'espace utilisateur reçoit une notification de défaut de page en cas d'accès à une page manquante (les pages qui n'ont pas encore fait l'objet d'une faute)
	// UFFDIO_REGISTER_MODE_WP: l'espace utilisateur reçoit une notification de défaut de page lorsqu'une page protégée en écriture est écriite. Nécéssaire pour le handle de défaut de page
	uffdio_register.range.start = (unsigned long)region;	// Début de la zone mémoire
	uffdio_register.range.len = num_page * PAGE_SIZE;		// Taille de la zone (nombre de page x taille d'une page): il faut que ce soit un multiple de PAGE_SIZE, sinon UFFDIO_REGISTER ne marchera pas
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;

	printf("[INFOS] %d pages couvertes par userfaultfd (%lld octets)\n", num_page, uffdio_register.range.len);

	// Enregistre et setup notre userfaultfd avec la plage donnée (communication avec le noyau)
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		perror("[ERROR][set_userfaultfd] ioctl/uffdio_register");
		exit(1);
	}

	// Démarrer le thread qui va gérer les défauts de pages et lui donne les paramètres requis
	handle_params->uffd = uffd;
	handle_params->nombre_page = num_page;
	handle_params->pointeurZoneMemoire = region;

	if(pthread_create(&pth_userfaultfd, NULL, handle_defaut, handle_params) != 0) {
		perror("[ERROR][set_userfaultfd] pthread_create");
		exit(1);
	}

	printf("[OK] Userfaultfd mise en place\n");
}

/**
 * Initialisation de l'esclave, créer le connexion et s'enregistre au maître,
 * met en place le userfaultfd pour les fautes de pages, et l'écoute sur un port
 * les autres esclaves.
 */
void* InitSlave(char* HostMaster) {
	int num_page;							// Nombre de pages allouées
	struct memoire_pages * temp_memoire; 	// Structure temporaire pour la création de la mémoire des pages
	long taille_recv;						// Taille de la mémoire partagée, reçue par le maître
	int* port;								// Port d'écoute de l'esclave

	port = malloc(sizeof(int));

	// Etablir une connexion avec le maître
	set_connection(HostMaster, port, &taille_recv);

	// Nouvelle zone mémoire de la taille reçue par le maître avec aucun droit d'écriture/lecture, servira à la gestion
	if((region = mmap(NULL, taille_recv, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		perror("[InitSlave] mmap");
		exit(2);
	}
	
	// Mise en place du mutex
	pthread_mutex_init(&mutex_esclave, NULL);

	// Calcule du nombre de pages que l'on a à allouer
	num_page = taille_recv / PAGE_SIZE;
    if((taille_recv % PAGE_SIZE) != 0){
		num_page++;
	}

    // Création de la structure mémoire qui a pour but de se rappeler des droits de lecture/écriture sur les pages et de leurs validitées
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

	// Démarrer un thread pour les demandes de pages depuis d'autres esclaves en cas d'invalidité de celles-ci
	if(pthread_create(&pth_esclave, NULL, LoopSlave, (void *)port)!= 0) {
		perror("[InitSlave] pthread_create");
    	exit(1);
    }

    // Mise en place du userfaultfd
	set_userfaultfd(num_page);
	
	sleep(1);
	return region;
}

/**
 * Trouver le numéro de page à partir d'une adresse
 */
long trouver_numero_page(void* data) {
	return ((data - region)*sizeof(*data))/PAGE_SIZE;
}

/**
 * Demande le droit au maître de verrouiller en lecture la(les)
 * page(s) où se trouve l'adrese précisée (bloquera si un écrivain
 * a déjà verrouillé la page)
 */
void lock_read(void* addr, int size) {
	pthread_mutex_lock(&mutex_esclave); 		//Met un mutex pour pas plusieurs lock/unlock en même temps.

	int socket_esclave; 						//Socket de communication avec un autre esclave
	int debut = trouver_numero_page(addr); 		//Première page de la demande de lecture
	int fin = trouver_numero_page(addr+size);	//Dernière page de la demande de lecture
	struct message message;						//message à envoyer au maître
	struct memoire_pages * temp_memoire; 		//Structure pour les info des pages

	// Création du message à envoyer.
	message.type = REQUETE_LOCK_READ;
	message.debut_page = debut;
	message.fin_page = fin;

	printf("[LOCK_READ] Je veux accéder en lecture aux pages P[%d-%d] (%d pages) !\n", message.debut_page, message.fin_page, message.fin_page-message.debut_page+1);

	//Demande de vérous sur les pages donnée dans le message.
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][lock_read] send (1)");
		exit(1);
	}

	printf("[LOCK_READ] Attente des écrivains...\n");
	//Partie bloquante du programme, attente que tout les écrivain des page on fini.
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][lock_read] recv (1)");
		exit(1);
	}
	printf("[LOCK_READ] Fin attente\n");

	//On vérifie que le message reçu est bien la confirmation rechercher et pas quelque chose de random (Vérification de synchronisation).
	/*if(message.type != ACK){
		printf("[ERROR][lock_read] Problème de synchronisation (%d)\n", message.type);
		exit(1);
	}*/

	printf("[LOCK_READ] Je peux accéder à(aux) page(s) !\n");

	//Pour chaque pages à obtenir le vérous.
	for(int i=debut ; i<=fin ; i++) {
		//On récupére les information de notre page.
		temp_memoire = trouver_mem_page(i);
		//Calcule le pointeur vers la page à vérouillé.
		void* data = addr - (addr-region) + PAGE_SIZE*i;

		//On change	les droit de lecture seulement si on n'a pas le droit de lecture actuellement ou que notre page n'est pas valide.
		if(temp_memoire->droit_read == 0 || temp_memoire->validite == 0){

			//Dans le cas où la page est invalide ou qu'on ne la jamais demandée.
			if(temp_memoire->validite == 0){
				//Donner le droit de lecture/écriture sur la page pour la copier
				if(mprotect(data, PAGE_SIZE, PROT_WRITE|PROT_READ) != 0){
					perror("[ERROR][lock_read] mprotect (1)");
					exit(1);
				}

				//Le maître envoie un message pour que l'esclave sache s'il va recevoir la page directement depuis le maître ou va devoir demander un autre esclave.
				if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
					perror("[ERROR][lock_read] recv (2)");
					exit(1); 
				}

				//Si on reçois la page directement du maître.
				if(message.type == RETOUR_DEMANDE_PAGE_ENVOIE){
					printf("[LOCK_READ] Recupération de P%d depuis le maître !\n", i);

					//On la copie au bonne emplacement.
					if(recv(maitre_fd, data, PAGE_SIZE, 0) == -1) {
						perror("[ERROR][lock_read] recv (3)");
						exit(1);
					}
				//Sinon, on doit allez la chercher sur l'esclave propriétaire.
				}else if(message.type == RETOUR_DEMANDE_PAGE_VERS_ESCLAVE){
					printf("[LOCK_READ] Recupération de P%d depuis un esclave !\n", i);
					//Ouvre un socket en TCP.
					if((socket_esclave = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
						perror("[ERROR][lock_read] socket");
						exit(1);
					}

					//On ce connecte à l'esclave (répéte si ne répond pas).
					while(connect(socket_esclave, (struct sockaddr*)&message.esclave_info, sizeof(message.esclave_info)) == -1) {
						perror("[ERROR][lock_read] connect");
						sleep(2);
					}

					//On envoie le numéro de page à recevoir à l'esclave
					message.type = REQUETE_DEMANDE_PAGE;
					message.debut_page = i;			
					if(send(socket_esclave, &message, sizeof(struct message),0) == -1){
						perror("[ERROR][lock_read] send (2)");
						exit(1);
					}

					//Reception de la page, mis au bonne emplacement.
					if(recv(socket_esclave, data, PAGE_SIZE, 0)== -1){
						perror("[ERROR][lock_read] recv (4)");
						exit(1);
					}

					//Ferme la connexion à l'esclave et le socket.
					if(shutdown(socket_esclave, SHUT_RDWR) == -1){
						perror("[ERROR][lock_read] shutdown");
					}
				
					if(close(socket_esclave)){
						perror("[ERROR][lock_read] close");
						exit(1);
					}

					//Confirme au maître que tout c'est bien passer (synchronisation).
					message.type = ACK;
					if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
						perror("[ERROR][lock_read] send (3)");
						exit(1);
					}
				}
				//Après la récupération, la page est donc valide et à jour.
				temp_memoire->validite = 1;
			}

			// Dans notre structure d'information des page, on met le droit de lecture (Pour pas a redemander plusieur fois). Et on met les droit de lecture sur la zone mémoire en question.
			if(mprotect(data, PAGE_SIZE, PROT_READ) != 0){
				perror("[ERROR][lock_read] mprotect (2)");
			}
			temp_memoire->droit_read = 1;
		}

		//Attente que le maître finise ses traitement (Pour que les deux soit synchronisée sur le résultat).
		if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
			perror("[ERROR][lock_read] recv (5)");
			exit(1);
		}

		//On vérifie que le message reçu est bien la confirmation rechercher et pas quelque chose de random (Vérification de synchronisation).
		/*if(message.type != ACK){
			printf("[ERROR][lock_read] Problème de synchronisation (%d)\n", message.type);
			exit(1);
		}*/

	}

	//Rend le mutex pour avoir d'autres actions lock/unlock
	pthread_mutex_unlock(&mutex_esclave);
}

/**
 * Rend le vérrou de la ou les pages en lecture.
 */
void unlock_read(void* addr, int size) {
	pthread_mutex_lock(&mutex_esclave); //Met un mutex pour pas plusieurs lock/unlock en même temps.

	struct message message; // Requête envoyée au maître.
	struct memoire_pages * temp_memoire; // Structure information des page que l'on touche.

	//Création du message à envoyer. Une requête pour enlever le vérrou sur les pages aux maître.
	message.type = REQUETE_UNLOCK_READ;
	message.debut_page = trouver_numero_page(addr); //Calcule l'adresse de la première page.
	message.fin_page = trouver_numero_page(addr+size); //Calcule l'adresse de la dernière page.

	//Demande de dévérouiller les pages en lecture aux maître.
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][unlock_read] send");
		exit(1);
	}

	//Pour chaque page à dévérouiller (traitement locale).
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		printf("[UNLOCK_READ] Je libère P%d !\n", i);
		//Calcule le pointeur vers la page à dévérouillé.
		void* data = addr - (addr-region) + PAGE_SIZE*i;
		//On récupére les information de notre page.
		temp_memoire = trouver_mem_page(i);

		// Retire le droit de lecture sur la page en question, si l'esclave essaye de lire après cette fonction, il va avoir un défaut de segmentation.
		if(mprotect(data, PAGE_SIZE, PROT_NONE) != 0){
			perror("[ERROR][unlock_read] mpeortect");
			exit(1);
		}

		// On change la variable pour dire que l'on à plus aucun droit sur la lecture (Donc on va devoir repasser par le maître si on veux un droit).
		temp_memoire->droit_read = 0;
	}

	printf("[UNLOCK_READ] Nombre de pages libérées: %d\n", message.fin_page-message.debut_page+1);

	//Attente que le maître finise ses traitement (Pour que les deux soit synchronisée sur le résultat).
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][unlock_read] recv");
		exit(1);
	}

	//On vérifie que le message reçu est bien la confirmation rechercher et pas quelque chose de random (Vérification de synchronisation).
	/*if(message.type != ACK){
		printf("[ERROR][unlock_read] Problème de synchronisation (%d)\n", message.type);
		exit(1);
	}*/

	//Rend le mutex pour avoir d'autres actions lock/unlock
	pthread_mutex_unlock(&mutex_esclave);
}

//Demande le droit au maître d'écrire sur la/les page(s) où ce trouve le "adr" de taille s (bloquant quand un écrivain ou lecteur deja présent).
void lock_write(void* addr, int size) {
	pthread_mutex_lock(&mutex_esclave); //Met un mutex pour pas plusieurs lock/unlock en même temps.

	int socket_esclave; 						//Socket de communication avec un autre esclave
	int debut = trouver_numero_page(addr); 		//Première page de la demande de lecture
	int fin = trouver_numero_page(addr+size);	//Dernière page de la demande de lecture
	struct message message;						//message à envoyer au maître
	struct memoire_pages * temp_memoire; 		//Structure pour les info des pages

	// Création du message à envoyer.
	message.type = REQUETE_LOCK_WRITE;
	message.debut_page = debut;
	message.fin_page = fin;

	printf("[LOCK_WRITE] Je veux accéder en écriture aux pages P[%d-%d] (%d pages) !\n", message.debut_page, message.fin_page, message.fin_page-message.debut_page+1);

	//Demande de vérous sur les pages donnée dans le message.
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][lock_write] send (1)");
		exit(1);
	}

	printf("[LOCK_WRITE] Attente de la fin des autres écrivains/lecteurs...\n");
	//Partie bloquante du programme, attente que tout les écrivain et lecteur des page on fini.
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][lock_write] recv (1)");
		exit(1);
	}
	printf("[LOCK_WRITE] Fin attente.\n");

	//On vérifie que le message reçu est bien la confirmation rechercher et pas quelque chose de random (Vérification de synchronisation).
	/*if(message.type != ACK){
		printf("[ERROR][lock_write] Problème de synchronisation (%d)\n", message.type);
		exit(1);
	}*/

	printf("[LOCK_WRITE] Je peux accéder à(aux) page(s) !\n");

	//Pour chaque pages à obtenir le lock.
	for(int i=debut ; i<=fin ; i++) {
		//On récupére les information de notre page.
		temp_memoire = trouver_mem_page(i);
		//Calcule le pointeur vers la page à vérouillé.
		void* data = addr - (addr-region) + PAGE_SIZE*i;

		//On change les droit d'écriture seulement si on n'a pas le droit d'écriture actuellement ou que notre page n'est pas valide.
		if(temp_memoire->droit_write == 0 || temp_memoire->validite == 0){
			
			//Dans le cas où la page est invalide ou qu'on ne la jamais demandée.
			if(temp_memoire->validite == 0){
				//Donne les droits de lecture/écriture temporairement juste pour copier la page.
				if(mprotect(data, PAGE_SIZE, PROT_READ|PROT_WRITE)!= 0){
					perror("[ERROR][lock_write] mprotect (1)");
					exit(1);
				}

				//Le maître envoie un message pour que l'esclave sache s'il va recevoir la page directement depuis le maître ou va devoir demander un autre esclave.
				if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
					perror("[ERROR][lock_write] recv (2)");
					exit(1);
				}

				//Si on reçois la page directement du maître.
				if(message.type == RETOUR_DEMANDE_PAGE_ENVOIE){
					printf("[LOCK_WRITE] Recupération de P%d depuis le maître !\n", i);

					//On la copie au bonne emplacement.
					if(recv(maitre_fd, data, PAGE_SIZE, 0) == -1) {
						perror("[ERROR][lock_write] recv (3)");
						exit(1);
					}
				//Sinon, on doit allez la chercher sur l'esclave propriétaire.
				}else if(message.type == RETOUR_DEMANDE_PAGE_VERS_ESCLAVE){
					printf("[LOCK_READ] Recupération de P%d depuis un esclave !\n", i);
					//Ouvre un socket en TCP.
					if((socket_esclave = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
						perror("[ERROR][lock_write] socket");
						exit(1);
					}

					//On ce connecte à l'esclave (répéte si ne répond pas).
					while(connect(socket_esclave, (struct sockaddr*)&message.esclave_info, sizeof(message.esclave_info)) == -1) {
						perror("[ERROR][lock_write] connect");
						sleep(2);
					}

					//On envoie le numéro de page à recevoir à l'esclave
					message.type = REQUETE_DEMANDE_PAGE;
					message.debut_page = i;			
					if(send(socket_esclave, &message, sizeof(struct message),0) == -1){
						perror("[ERROR][lock_write] send (2)");
						exit(1);
					}

					//Reception de la page, mis au bonne emplacement.
					if(recv(socket_esclave, data, PAGE_SIZE, 0)== -1){
						perror("[ERROR][lock_write] recv (4)");
						exit(1);
					}

					//Ferme la connexion à l'esclave et le socket.
					if(shutdown(socket_esclave, SHUT_RDWR) == -1){
						perror("[ERROR][lock_write] shutdown");
					}
				
					if(close(socket_esclave)){
						perror("[ERROR][lock_write] close");
						exit(1);
					}

					//Confirme au maître que tout c'est bien passer (synchronisation).
					message.type = ACK;
					if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
						perror("[ERROR][lock_write] send (3)");
						exit(1);
					}
				}
				//Après la récupération, la page est donc valide et à jour.
				temp_memoire->validite = 1;
			}

			// Dans notre structure d'information des page, on met le droit d'écriture. (Pour pas a redemander plusieur fois). Et on met les droit d'écriture sur la zone mémoire en question.
			if(mprotect(data, PAGE_SIZE, PROT_WRITE) != 0){
				perror("[ERROR][lock_write] mprotect (2)");
			}
			temp_memoire->droit_write = 1;
		}

		//Attente que le maître finise ses traitement (Pour que les deux soit synchronisée sur le résultat).
		if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
			perror("[ERROR][lock_write] recv (5)");
			exit(1);
		}

		//On vérifie que le message reçu est bien la confirmation rechercher et pas quelque chose de random (Vérification de synchronisation).
		/*if(message.type != ACK){
			printf("[ERROR][lock_write] Problème de synchronisation (%d)\n", message.type);
			exit(1);
		}*/
	}	

	//Rend le mutex pour avoir d'autres actions lock/unlock
	pthread_mutex_unlock(&mutex_esclave);
}

/**
 * Rend le vérrou de la ou les pages en écriture.
 */
void unlock_write(void* addr, int size) {
	pthread_mutex_lock(&mutex_esclave); //Met un mutex pour pas plusieurs lock/unlock en même temps.

	struct message message; // Requête envoyée au maître.
	struct memoire_pages * temp_memoire; // Structure information des page que l'on touche.

	//Création du message à envoyer. Une requête pour enlever le vérrou en écriture sur les pages aux maître.
	message.type = REQUETE_UNLOCK_WRITE;
	message.debut_page = trouver_numero_page(addr); //Calcule l'adresse de la première page.
	message.fin_page = trouver_numero_page(addr+size); //Calcule l'adresse de la dernière page.

	//Demande de dévérouiller les pages en écriture aux maître.
	if(send(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][unlock_write] send");
		exit(1);
	}

	//Pour chaque page à dévérouiller (traitement locale).
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		//Calcule le pointeur vers la page à dévérouillé.
		void* data = addr - (addr-region) + PAGE_SIZE*i;
		//On récupére les information de notre page.
		temp_memoire = trouver_mem_page(i);
		printf("[UNLOCK_WRITE] Je libère P%d !\n", i);
		// Retire le droit d'écriture sur la page en question, si l'esclave essaye d'écrire après cette fonction, il va avoir un défaut de segmentation.
		if(mprotect(data, PAGE_SIZE, PROT_NONE) != 0){
			perror("[ERROR][unlock_write] mprotect");
			exit(1);
		}

		// On change la variable pour dire que l'on à plus aucun droit sur l'écriture (Donc on va devoir repasser par le maître si on veux un droit).
		temp_memoire->droit_write = 0;
	}

	printf("[UNLOCK_WRITE] Nombre de pages libérées: %d\n", message.fin_page-message.debut_page+1);

	//Attente que le maître finise ses traitement (Pour que les deux soit synchronisée sur le résultat).
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][unlock_write] recv");
		exit(1);
	}

	//On vérifie que le message reçu est bien la confirmation rechercher et pas quelque chose de random (Vérification de synchronisation).
	/*if(message.type != ACK){
		printf("[ERROR][UNLOCK_WRITE] Problème de synchronisation (%d)\n", message.type);
		exit(1);
	}*/

	//Rend le mutex pour avoir d'autres actions lock/unlock
	pthread_mutex_unlock(&mutex_esclave);
}

/**
 * Rendre les pages au maître, mettre fin aux threads et libérer la mémoire
 */
void endSlave(void* data, int size){
	printf("[~] Tentative de terminaison de l'esclave...\n");
	
	int nombre_page;
	int nombre_page_rendre; 				// Contient le nombre de page que l'on va rendre au maître
	int requete; 							// Message envoyé/reçu
	struct memoire_pages * temp_memoire; 	// Structure temporaire pour la libération de la mémoire info

	// Arrêt du thread gérant les invalidités de page
    if (pthread_cancel(pth_esclave) != 0){
    	perror("[ERROR][endSlave] pthread_cancel (1)");
		exit(1);
	}
    // Arrêt du thread gérant userfaulfd
	if (pthread_cancel(pth_userfaultfd) != 0){
		perror("[ERROR][endSlave] pthread_cancel (2)");
		exit(1);
	}

	// Demande de déclenchement de terminaison au maître
	printf("[~] Demande d'arrêt envoyée au maître...\n");
	requete = REQUETE_FIN;
	if(send(maitre_fd, &requete, sizeof(int), 0) == -1){
		perror("[ERROR][endSlave] send (1)");
		exit(1);
	}

	// Réception du nombre de pages modifiées que devra réceptionner le maître de la part de l'esclave actuel
	printf("[~] Envoi des pages au maître...\n");
	if(recv(maitre_fd, &nombre_page_rendre, sizeof(int), 0) == -1){
		perror("[ERROR][endSlave] recv (1)");
		exit(1);
	}

	// Envoyer les pages modifiées au maître
	while(nombre_page_rendre != 0) {

		// Numéro de la page à recevoir
		if(recv(maitre_fd, &nombre_page, sizeof(int), 0)== -1){
			perror("[ERROR][endSlave] recv (2)");
			exit(1);
		}

		// Adresse de début de page
		void* data = region + PAGE_SIZE*nombre_page;
		
		// Attribuer le droit de lecture sur la page concernée
		if(mprotect(data, PAGE_SIZE, PROT_READ) != 0){
			perror("[ERROR][endSlave] mprotect (1)");
			exit(1);
		}

		// Envoi de la page concernée
		if(send(maitre_fd, data, PAGE_SIZE, 0)== -1){
			perror("[ERROR][endSlave] send (2)");
			exit(1);
		}

		// Retirer les droits sur la page concernée
		if(mprotect(data, PAGE_SIZE, PROT_NONE) != 0){
			perror("[ERROR][endSlave] mprotect (2)");
			exit(1);
		}

		nombre_page_rendre--;

		printf("[OK] P%d envoyée au maître\n", nombre_page);
	}

	// Attente un acquittement du maître
	struct message message;
	if(recv(maitre_fd, &message, sizeof(struct message), 0) == -1) {
		perror("[ERROR][endSlave] recv (3)");
		exit(1);
	}

	/*if(message.type != ACK){
		printf("[ERROR][endSlave] Problème de synchronisation (%d)\n", message.type);
		exit(1);
	}*/

	// Fermeture de la connexion
	if(shutdown(maitre_fd, SHUT_RDWR) == -1){
		perror("[ERROR][endSlave] shutdown");
		exit(1);
	}

	if(close(maitre_fd) == -1){
		perror("[ERROR][endSlave] close");
		exit(1);
	}

	// Libèrer la mémoire partagée locale
	if(munmap(data, size) == -1){
		perror("[ERROR][endSlave] munmap");
	}

	// Libèrer la mémoire info
	temp_memoire = memoire;
	while(temp_memoire != NULL){
		memoire = temp_memoire->next;
		free(temp_memoire);
		temp_memoire = memoire;
	}

	printf("[OK] Terminaison de l'esclave ! A bientôt !\n");
}
