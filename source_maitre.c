/***** LIBRAIRIES *****/

#include <sys/syscall.h>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib.h"


/***** VARIABLES GLOBALES *****/

static struct SMEMORY * memory_data; //Mémoire contenant toutes les méta-données de nos page et esclaves
pthread_cond_t cond_lecteurs 	= PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_ecrivains 	= PTHREAD_COND_INITIALIZER;


/***** FONCTIONS *****/


// --- LISTE ESCLAVES ---

void afficher_esclaves() {
	struct liste_esclaves* actuel = memory_data->list_esclaves;

	printf("***** liste_esclaves: [FD] =");
	while(actuel != NULL) {
		printf(" %d", actuel->esclave->fd);
		actuel = actuel->suivant;
	}
	printf("\n");
}

// Ajouter un esclave spécifique dans le système
struct esclave* ajouter_esclave(int fd, struct sockaddr_in adresse) {
	// Créer un esclave avec les informations fournies
	struct esclave* esclave = malloc(sizeof(struct esclave));
	esclave->fd = fd;
	esclave->info = adresse;

	// Créer un chaînon qui se met en tête de la liste
	struct liste_esclaves* le = malloc(sizeof(struct liste_esclaves));
	le->esclave = esclave;
	le->suivant = memory_data->list_esclaves;
	memory_data->list_esclaves = le;

	memory_data->nombre_esclaves_actuel++;

	/*
	afficher_esclaves();
	*/

	return esclave;
}

// Supprimer un esclave spécifique dans le système
void supprimer_esclave(struct esclave* esclave) {
	struct liste_esclaves* actuel = memory_data->list_esclaves;
	struct liste_esclaves* precedent = NULL;

	while(actuel != NULL) {
		if(actuel->esclave->fd == esclave->fd) {
			if(precedent == NULL) {
				memory_data->list_esclaves = actuel->suivant;
				break;
			}
			else {
				precedent->suivant = actuel->suivant;
				break;
			}
		}
		precedent = actuel;
		actuel = actuel->suivant;
	}

	if(actuel != NULL)
		memory_data->nombre_esclaves_actuel--;

	free(actuel->esclave);
	free(actuel);
}

// Désallouer les composants de la liste d'esclaves
void desallouer_liste_esclaves() {
	while(memory_data->list_esclaves != NULL) {
		struct liste_esclaves* le = memory_data->list_esclaves->suivant;
		free(memory_data->list_esclaves->esclave);
		free(memory_data->list_esclaves);
		memory_data->list_esclaves = le;
	}
}

// --- LECTEURS ---

void afficher_lecteurs(struct lecteur* lecteurs) {
	struct lecteur* actuel = lecteurs;

	printf("- lecteurs_actuels: [FD] =");
	while(actuel != NULL) {
		printf(" %d", actuel->esclave->fd);
		actuel = actuel->suivant;
	}
	printf("\n");
}

int je_suis_lecteur(struct page* page, struct esclave* esclave) {
	struct lecteur* l = page->lecteurs_actuels;
	int je_suis_lecteur = 0;

	while(l != NULL) {
		if(l->esclave->fd == esclave->fd) {
			je_suis_lecteur = 1;
			break;
		}
		l = l->suivant;
	}

	return je_suis_lecteur;
}

// Ajouter un lecteur dans la liste des lecteurs actuels + dans le cache si celui-ci n'y figure pas
void _ajouter_lecteur(struct lecteur** lecteurs, struct esclave* esclave, struct page* page) {
	struct lecteur* actuel = *lecteurs;

	while(actuel != NULL) {
		if(actuel->esclave->fd == esclave->fd)
			break;
		actuel = actuel->suivant;
	}

	if(actuel == NULL) {
		struct lecteur *nouveau_premier = malloc(sizeof(struct lecteur));
		nouveau_premier->esclave = esclave;
		nouveau_premier->suivant = *lecteurs;

		*lecteurs = nouveau_premier;

		if(page != NULL)
			page->nombre_reader++;
	}
}

// Ajouter un lecteur sur une page spécifique
void ajouter_lecteur(struct page* page, struct esclave* esclave) {
	_ajouter_lecteur(&page->lecteurs_actuels, esclave, page);
	_ajouter_lecteur(&page->lecteurs_cache, esclave, NULL);

	//
	//afficher_lecteurs(page->lecteurs_actuels);
	//afficher_lecteurs(page->lecteurs_cache);
	//
}

// Supprimer un lecteur spécifique de la liste des lecteurs désirée
void _supprimer_lecteur(struct lecteur** lecteurs, struct esclave* esclave, struct page* page) {
	struct lecteur *actuel = *lecteurs;
	struct lecteur *precedent = NULL;

	while(actuel != NULL) {
		if(actuel->esclave->fd == esclave->fd) {
			if(precedent == NULL) {
				*lecteurs = actuel->suivant;
				break;
			}
			else {
				precedent->suivant = actuel->suivant;
				break;
			}
		}
		precedent = actuel;
		actuel = actuel->suivant;
	}

	if( (page != NULL) && (actuel != NULL) ) {
		page->nombre_reader--;
	}

	free(actuel);
}

// Supprimer un lecteur du cache d'une page spécifique
void supprimer_lecteur_actuel(struct page* page, struct esclave* esclave) {
	_supprimer_lecteur(&page->lecteurs_actuels, esclave, page);
	//
	//afficher_lecteurs(page->lecteurs_actuels);
	//
}

// Supprimer un lecteur actuel d'une page spécifique
void supprimer_lecteur_cache(struct page* page, struct esclave* esclave) {
	_supprimer_lecteur(&page->lecteurs_cache, esclave, NULL);
	//
	//afficher_lecteurs(page->lecteurs_cache);
	//
}

// Désallouer une liste de lecteurs spécifique
void _desallouer_lecteurs(struct lecteur** lecteurs) {
	struct lecteur *actuel = *lecteurs;
	struct lecteur *precedent = NULL;

	while(actuel != NULL) {
		precedent = actuel;
		actuel = actuel->suivant;
		free(precedent);
	}

	free(actuel);
	*lecteurs = NULL;
}

// Supprimer tous les lecteurs présents dans le cache d'une page spécifique
void supprimer_lecteurs_cache(struct page* page) {
	_desallouer_lecteurs(&page->lecteurs_cache);
}

// Désallouer toutes les listes de lecteurs d'une page
void desallouer_lecteurs(struct page* page) {
	_desallouer_lecteurs(&page->lecteurs_actuels);
	_desallouer_lecteurs(&page->lecteurs_cache);
}

//Cache 
int est_dans_cache(struct esclave* esclave, struct page* page){
	struct lecteur* l = page->lecteurs_cache;	
	while(l != NULL) {
		if(l->esclave->fd == esclave->fd) {
			break;
		}
		l = l->suivant;
	}

	afficher_lecteurs(page->lecteurs_cache);
	return (l != NULL);
}

void invalider_page(struct page* page, int numero) {
	struct lecteur* lecteur = page->lecteurs_cache;
	struct message message;
	int socket_esclave = 0;

	printf("- Invalidation des pages\n");

	while(lecteur != NULL) {
		message.type = REQUETE_INVALIDE_PAGE;
		message.debut_page = numero;
		//Ouverture d'un socket TCP
		if(lecteur->esclave != page->ancient_ecrivain){
			if((socket_esclave = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
				perror("Problème d'initialisation du socket\n");
				exit(1);
			}

			//Tant que pas connecter à l'esclave on réessaye
			printf("fd : %d \n", lecteur->esclave->fd);
			while(connect(socket_esclave, (struct sockaddr*)&lecteur->esclave->info, sizeof(lecteur->esclave->info)) == -1) {
				perror("Connection échouer");
				sleep(2);
			}



			printf("Envoie page\n");
			if(send(socket_esclave, &message, sizeof(struct message), 0) == -1) {
				perror("send");
				exit(1);
			}

			printf("Attente de reception de ack\n");
			if(recv(socket_esclave, &message, sizeof(struct message), 0) == -1) {
				perror("read");
				exit(1);
			}
			printf("Ack reçu\n");

			if(message.type != ACK){
				perror("Erreur de communication");
			}

			shutdown(socket_esclave, SHUT_RDWR);
			close(socket_esclave);
		}

		lecteur = lecteur->suivant;
	}

	printf("- Page %d invalider\n", numero);
	supprimer_lecteurs_cache(page);
}

void do_lock_read(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre);
	struct message requete;
	int dans_cache;

	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		//
		printf("- M%d veux accéder à la page P%d\n", esclave->fd, i);
		//
		// Il y a 1 écrivain sur la page, impossible de lire donc attendre sur la condition
		while(memory_data->tab_page[i]->ecrivain != NULL) {
			pthread_cond_wait(&cond_lecteurs, &memory_data->mutex_maitre);
			i = message.debut_page;
		}
	}

	//Dire qu'il n'est plus en attente
	requete.type = ACK;
	if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	//
	printf("- M%d peux accéder à(aux) page(s) !\n", esclave->fd);
	//

	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		printf("- Recherche dans le cache\n");
		dans_cache = est_dans_cache(esclave, memory_data->tab_page[i]);
		// Envoyer la page
		printf("(Debug : Dans cache ? %d/Esclave : %d/Ecrivain null? %d)\n", dans_cache, esclave->fd, (memory_data->tab_page[i]->ecrivain == NULL));
		if(dans_cache == 0 && esclave != memory_data->tab_page[i]->ecrivain){
			if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
				printf("- Envoie de page numéro : %d!\n", i);
				requete.type = RETOUR_DEMANDE_PAGE_ENVOIE;
				
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("write");
					exit(1);
				}

				printf("- Envoie de page n°%d\n", i);
				if(send(esclave->fd, memory_data->tab_page[i]->pointer, PAGE_SIZE, 0) == -1) {
					perror("write");
					exit(1);
				}
				
			}else{
				printf("- Redirection vers esclave (Page : %d, Esclave : %d)!\n", i, memory_data->tab_page[i]->ancient_ecrivain->fd);
				requete.type = RETOUR_DEMANDE_PAGE_VERS_ESCLAVE;
				requete.esclave_info = memory_data->tab_page[i]->ancient_ecrivain->info; 
				
				
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("write");
					exit(1);
				}

				printf("- Attente de ACK\n");
				if(recv(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("write");
					exit(1);
				}

				if(requete.type != ACK){
					printf("Erreur de sync (type : %d)\n", requete.type);
					exit(1);
				}
			}
		}else{
			printf("- Est dans le cache, pas besoin d'envoyer\n");
		}
		
		
		ajouter_lecteur(memory_data->tab_page[i], esclave);
		afficher_lecteurs(memory_data->tab_page[i]->lecteurs_actuels);

		requete.type = ACK;
		if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
			perror("write");
			exit(1);
		}

		//
		printf("- Nombre de lecteurs (P%d): %d\n", i, memory_data->tab_page[i]->nombre_reader);
		//afficher_lecteurs(memory_data->tab_page[i]->lecteurs_actuels);
		//
	}

	pthread_cond_signal(&cond_lecteurs);
	pthread_mutex_unlock(&memory_data->mutex_maitre);
}



void do_unlock_read(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre);

	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		if(je_suis_lecteur(memory_data->tab_page[i], esclave)) {
			printf("- M%d libère la page P%d\n", esclave->fd, i);
			supprimer_lecteur_actuel(memory_data->tab_page[i], esclave);

			//
			printf("- Nombre de lecteurs après suppression de M%d (P%d): %d\n", esclave->fd, i,  memory_data->tab_page[i]->nombre_reader);
			//
		} else {
			printf("- M%d essaie d'effectuer une action sans en avoir les droits !\n", esclave->fd);
			break;
		}
	}

	struct message reponse;
	reponse.type = ACK;
	if(send(esclave->fd, &reponse, sizeof(struct message), 0) == -1) {
		perror("send");
		exit(1);
	}

	// Réveiller les écrivains/lecteurs bloqués
	pthread_cond_signal(&cond_lecteurs);
	pthread_cond_signal(&cond_ecrivains);

	pthread_mutex_unlock(&memory_data->mutex_maitre);
}

void do_lock_write(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre);

	struct message requete;

	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		//
		printf("- M%d veux accéder à la page P%d\n", esclave->fd, i);
		//
		while( (memory_data->tab_page[i]->ecrivain != NULL && memory_data->tab_page[i]->ecrivain != esclave) || memory_data->tab_page[i]->nombre_reader > 0) {
			pthread_cond_wait(&cond_ecrivains, &memory_data->mutex_maitre);
			i = message.debut_page;
		}
	}

	//Dire qu'il n'est plus en attente
	requete.type = ACK;
	if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}

	//
	printf("- M%d peux accéder à(aux) page(s) !\n", esclave->fd);
	//
	
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		memory_data->tab_page[i]->ecrivain = esclave;
		//
		printf("- Nombre de lecteurs (P%d) = %d\n", i, memory_data->tab_page[i]->nombre_reader);
		//

		// Envoyer la page

		int dans_cache = est_dans_cache(esclave, memory_data->tab_page[i]);
		// Envoyer la page
		if(dans_cache == 0 && memory_data->tab_page[i]->ancient_ecrivain != esclave ){
			if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
				printf("- Envoie de page numéro : %d!\n", i);
				requete.type = RETOUR_DEMANDE_PAGE_ENVOIE;
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("write");
					exit(1);
				}

				if(send(esclave->fd, memory_data->tab_page[i]->pointer, PAGE_SIZE, 0) == -1) {
					perror("write");
					exit(1);
				}
				
			}else{
				printf("- Redirection vers esclave : %d!\n", i);
				requete.type = RETOUR_DEMANDE_PAGE_VERS_ESCLAVE;
				requete.esclave_info = memory_data->tab_page[i]->ancient_ecrivain->info; 
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("write");
					exit(1);
				}

				if(recv(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("write");
					exit(1);
				}

				if(requete.type != ACK){
					perror("communication");
				}
			}
			
		}else{
			printf("Est deja écrivain ou avait deja la page, pas besoin d'envoyer\n");
		}

		message.type = ACK;
		if(send(esclave->fd, &message, sizeof(struct message), 0) == -1) {
			perror("send");
			exit(1);
		}
	}
	printf("- Fin lock_write\n");
	pthread_mutex_unlock(&memory_data->mutex_maitre);
}

void do_unlock_write(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre);

	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		if(memory_data->tab_page[i]->ecrivain == esclave) {
			// informer les lecteurs de l'invalidité de la page
			
			memory_data->tab_page[i]->ecrivain = NULL;
			memory_data->tab_page[i]->ancient_ecrivain = esclave;
			
			invalider_page(memory_data->tab_page[i], i);
			_ajouter_lecteur(&memory_data->tab_page[i]->lecteurs_cache, esclave, NULL);

			//
			printf("- M%d libère la page P%d !\n", esclave->fd, i);
			//
		} else {
			printf("- M%d essaie d'effectuer une action sans en avoir les droits !\n", esclave->fd);
		}
	}

	struct message reponse;
	reponse.type = ACK;

	if(send(esclave->fd, &reponse, sizeof(struct message), 0) == -1) {
		perror("send");
		exit(1);
	}

	pthread_cond_signal(&cond_lecteurs);
	pthread_cond_signal(&cond_ecrivains);

	pthread_mutex_unlock(&memory_data->mutex_maitre);
}



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
    memory_data->list_esclaves = NULL; //NULL est la dernière entrée dans la liste chaînée
    memory_data->size = size;
	memory_data->nombre_esclaves_actuel = 0;
	memory_data->nombre_page = size/PAGE_SIZE;
	modulo = size%PAGE_SIZE;

	printf("-Table info sur les pages: Création\n");
    //On remplie la valeur globale jusqu'a que l'on à le nombre de page que l'on cherche
	for(int i = 0; i <= (size/PAGE_SIZE); i++){
		if((memory_data->tab_page[i] = (struct page *)malloc(sizeof(struct page))) == NULL){
			perror("Erreur malloc!\n");
		}
		memory_data->tab_page[i]->lecteurs_actuels = NULL;
		memory_data->tab_page[i]->lecteurs_cache = NULL;
		memory_data->tab_page[i]->nombre_reader = 0; 
		memory_data->tab_page[i]->ecrivain = NULL;  //écrivain sur la page, propriétaire
		memory_data->tab_page[i]->ancient_ecrivain = NULL; 
		memory_data->tab_page[i]->pointer = addr_zone+(i*PAGE_SIZE);

		//Initier le surplus, quand on ne remplie pas une page en entière
		if(i == (size/PAGE_SIZE) && modulo != 0){
			memory_data->nombre_page += 1;			
			if((memory_data->tab_page[i+1] = (struct page *)malloc(sizeof(struct page))) == NULL){
				perror("Erreur malloc!\n");
			}
			memory_data->tab_page[i+1]->lecteurs_actuels = NULL;
			memory_data->tab_page[i+1]->lecteurs_cache = NULL;
			memory_data->tab_page[i+1]->nombre_reader = 0;
			memory_data->tab_page[i+1]->ecrivain = NULL;
			memory_data->tab_page[i+1]->ancient_ecrivain = NULL; 
			memory_data->tab_page[i+1]->pointer = addr_zone+((i+1)*PAGE_SIZE);
			
		}
	}
	printf("-%d pages de %d octets\n- Maître initialisée\n\n", memory_data->nombre_page, PAGE_SIZE);
	return addr_zone;
}


//Répond à des requête d'un esclave
static void *slaveProcess(void * param){
	//Détache le processus, pas besoin de join plus tard dans le loopMaster
	pthread_detach(pthread_self());
	
	struct esclave esclave_actuel = *((struct esclave *)param); //Descripteur pour la communication de un esclave, et ses info
	int esclave_end = 0; // Valeur qui signal la fin de la communication
	int nombre_de_page; // Nombre de page que l'esclave doit rendre (REQUETE_FIN)
	struct message msg;

	//Verifie si InitMaster à bien été appeler
	if(memory_data == NULL){
		perror("Memoire non-initialisé");
	}

	printf("[fd : %d]Traiment d'un esclave(n°thread) : %ld\n", esclave_actuel.fd ,syscall(SYS_gettid));
	while(esclave_end != 1){
		//Réinitialise la valeur a chaque boucle.
		msg.type = -1;
		//Attende de la reception d'une rêquete
		if(recv(esclave_actuel.fd, &msg, sizeof(struct message),0)== -1){
			perror("Problème communication 1");
            esclave_end = 1;
		}

		//Switch_case pour savoir le comportement à prendre par rapport à la requête
		switch (msg.type){
		//L'esclave veut s"enregistrer, rentrer dans la liste des esclave, on lui renvoie la taille de la zone mémoire en retour
		case REQUETE_INIT:
			//pthread_mutex_lock(&memory_data->mutex_maitre);
			printf("[fd : %d]Initialisation client, envoie de taille\n", esclave_actuel.fd);
			esclave_actuel.info.sin_port = msg.port;
			printf("- Port d'écoute de l'esclave : %d\n", msg.port);
			
			ajouter_esclave(esclave_actuel.fd, esclave_actuel.info);
			
			//Envoie de la taille
			if(send(esclave_actuel.fd, &memory_data->size, sizeof(long), 0) == -1){
				perror("Problème communication 2");
                esclave_end = 1;
			}
			//pthread_mutex_unlock(&memory_data->mutex_maitre);
			break;
		//L'esclave demande une page car il est dans un défaut de page, et il la nécéssite (ceci assume qu'il à deja les droits, sinon il aurais fait un SEGFAULT avant)
		case REQUETE_DEMANDE_PAGE: 
            /*printf("[fd : %d]Demande de page (lecture)\n", esclave_actuel.fd);
			
			//Recevoir le numéro de la page à envoyer
			if(recv(esclave_actuel.fd, &numero_page, sizeof(int),0)== -1){
				perror("Problème communication 3");
                esclave_end = 1;
			}
			printf("[fd : %d]Page numéro %d\n", esclave_actuel.fd,numero_page);

			//Qui est le propriétaire de cette page ? Si NULL : maître 
			if(memory_data->tab_page[numero_page]->ecrivain == NULL){
				//Répondre en disant que on envoie la page directement
				requete = RETOUR_DEMANDE_PAGE_ENVOIE;
				if(send(esclave_actuel.fd, &requete, sizeof(int), 0)== -1){
					perror("Problème communication 4");
                	esclave_end = 1;
					goto fin1;
				}

				//Puis on envoye la page entière
				pthread_mutex_lock(&memory_data->mutex_maitre);
				if(send(esclave_actuel.fd,memory_data->tab_page[numero_page]->pointer, PAGE_SIZE, 0)== -1){
					perror("Problème communication 5");
                	esclave_end = 1;
				}
				fin1:
				pthread_mutex_unlock(&memory_data->mutex_maitre);*
			//Le propriétaire n'est pas maître
			}else{
				//Dire qu'on envoye les info d'un esclave
				requete = RETOUR_DEMANDE_PAGE_VERS_ESCLAVE;

				//Vérifier que le propriétaire n'est pas lui même (récupere les info sur l'esclave)
				pthread_mutex_lock(&memory_data->mutex_maitre); //Ne faut pas que liste d'esclave soit pas toucher pendant ce temps
				tempEsclave = memory_data->list_esclaves;
				while(tempEsclave->esclave->fd != esclave_actuel.fd){
					tempEsclave = tempEsclave->suivant;
				}

				//Si il est lui même propriétaire, erreur
				if(memory_data->tab_page[numero_page]->ecrivain->fd == tempEsclave->esclave->fd){
					printf("[fd : %d]Erreur, propriétaire est celui qui demande???\n", esclave_actuel.fd);
					requete = -1;
				}

				//Envoie de la demande : eclave à la page
				if(send(esclave_actuel.fd, &requete, sizeof(int), 0)== -1){
					perror("Problème communication 6");
                	esclave_end = 1;
					goto fin2;
				}

				//Envoyer le propriétaire de la page
				if(send(esclave_actuel.fd,memory_data->tab_page[numero_page]->ecrivain, sizeof(struct sockaddr), 0)== -1){
					perror("Problème communication 7");
                	esclave_end = 1;
				}
				fin2:
				pthread_mutex_unlock(&memory_data->mutex_maitre);
			}
			break;	*/
		case REQUETE_LOCK_READ:
			printf("[fd : %d]LOCK READ\n", esclave_actuel.fd);
			do_lock_read(msg, &esclave_actuel);
			break;
		case REQUETE_LOCK_WRITE:
			printf("[fd : %d]LOCK WRITE\n", esclave_actuel.fd);
			//pages_modif[msg]
			do_lock_write(msg, &esclave_actuel);
			break;
		case REQUETE_UNLOCK_READ:
			printf("[fd : %d]UNLOCK READ\n", esclave_actuel.fd);
			do_unlock_read(msg, &esclave_actuel);
			break;
		case REQUETE_UNLOCK_WRITE:
			printf("[fd : %d]UNLOCK WRITE\n", esclave_actuel.fd);
			do_unlock_write(msg, &esclave_actuel);
			break;
		//L'esclave veut mettre fin à la connexion et se désenregistrer, il doit donc rendre les pages qui lui appartienne
		case REQUETE_FIN: // -> A faire
			pthread_mutex_lock(&memory_data->mutex_maitre); //La liste ne doit pas être toucher
			printf("[fd : %d]Fin de l'esclave, récupération des pages\n",  esclave_actuel.fd);
			esclave_end = 1;
			
			nombre_de_page = 0;
			for(int i = 0; i != memory_data->nombre_page; i++){
				if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
					continue;
				}
				else if(esclave_actuel.fd == memory_data->tab_page[i]->ancient_ecrivain->fd){
					nombre_de_page++;
				}
			}

			if(send(esclave_actuel.fd, &nombre_de_page, sizeof(int), 0) == -1){
				perror("Problème communication 8\n");
                esclave_end = 1;
			}

			for(int i = 0; i != memory_data->nombre_page; i++){
				if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
					continue;
				}
				else if(esclave_actuel.fd == memory_data->tab_page[i]->ancient_ecrivain->fd){
					memory_data->tab_page[i]->ancient_ecrivain=NULL;

					if(send(esclave_actuel.fd, &i, sizeof(int), 0) == -1){
						perror("Problème communication 9\n");
                		esclave_end = 1;
					}

					if(recv(esclave_actuel.fd, memory_data->tab_page[i]->pointer, PAGE_SIZE, 0) == -1){
						perror("Problème communication 10\n");
                		esclave_end = 1;
					}
					printf("Page %d reçu\n", i);
				}
			}

			msg.type = ACK;
			if(send(esclave_actuel.fd, &msg, sizeof(int), 0) == -1){
				perror("Problème communication 9\n");
            	esclave_end = 1;
			}
			//Enlève l'esclave de la liste
			struct esclave* esclave = malloc(sizeof(esclave));
			esclave->fd = esclave_actuel.fd;
			
			for(int i = 0; i != memory_data->nombre_page; i++){
				supprimer_lecteur_cache(memory_data->tab_page[i], esclave);
				afficher_lecteurs(memory_data->tab_page[i]->lecteurs_cache);
			}

			supprimer_esclave(esclave);
			pthread_mutex_unlock(&memory_data->mutex_maitre);
			break;		
		default:
			//Cas anormale, rien faire
			break;
		}
	}
	printf("[fd : %d]Fin traitement\n", esclave_actuel.fd);
	//Ferme la connexion
	//Fermeture du socket
	if(shutdown(esclave_actuel.fd, SHUT_RDWR) == -1){
		perror("shutdown");
	}
				
	if(close(esclave_actuel.fd)){
		perror("shutdown");
		exit(1);
	}
	return NULL;
}


/* Boucle du maître:
- Ce rappeller des esclave
- Envoyer une copie des pages au esclave
- Invalidité les copie des esclave quand une nouvelle page est écrite*/
void LoopMaster() {
	printf("Début de la boucle Maitre :\n");
	int socketEcoutefd, socketEsclaveFD;
	struct esclave param;
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
			//Mettre en place les paramètre pour la gestion de l'esclave
			param.fd = socketEsclaveFD;
			param.info = addrclt2;
			//Thread pour la gestion du nouveau esclave
			pthread_create(&th, NULL, slaveProcess, (void *)&param);	
		}
	}	
	
	//Fermeture du socket
	if(shutdown(socketEcoutefd, SHUT_RDWR) == -1){
		perror("shutdown");
	}
				
	if(close(socketEcoutefd)){
		perror("shutdown");
		exit(1);
	}
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
	
	desallouer_liste_esclaves();

	//Libère la mémoire de chaque struct page dans la struct SMEMORY
	for(int i = 0; i <= memory_data->nombre_page; i++){
		free(memory_data->tab_page[i]);
	}

	//Libère la mémoire que prend la struct MEMORY
	free(memory_data);
	printf("- Mémoire libèrer\n\n");
}