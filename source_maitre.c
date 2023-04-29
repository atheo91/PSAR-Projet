/***** LIBRAIRIES *****/

#include <sys/syscall.h>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib.h"


/***** VARIABLES GLOBALES *****/

static struct SMEMORY * memory_data; 						// Mémoire contenant toutes les méta-données de nos pages et esclaves
pthread_cond_t cond_lecteurs 	= PTHREAD_COND_INITIALIZER;	// Condition variable permettant aux threads qui veulent effectuer un LOCK_READ de s'endormir tant que la page n'est pas disponible
pthread_cond_t cond_ecrivains 	= PTHREAD_COND_INITIALIZER;	// Condition variable permettant aux threads qui veulent effectuer un LOCK_WRITE de s'endormir tant que la page n'est pas disponible


/***** FONCTIONS *****/

/**
 * Afficher de tous les esclaves connectés
 */
void afficher_esclaves() {
	struct liste_esclaves* actuel = memory_data->list_esclaves;

	printf("[INFOS] Esclaves actuellement connectés: [FD] = {");
	while(actuel != NULL) {
		printf(" %d", actuel->esclave->fd);
		actuel = actuel->suivant;
	}
	printf(" }\n");
}

/**
 * Ajouter un esclave spécifique dans le système
 */
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

	afficher_esclaves();

	return esclave;
}

/**
 * Supprimer un esclave spécifique dans le système
 */
void supprimer_esclave(struct esclave* esclave) {
	struct liste_esclaves* actuel = memory_data->list_esclaves;
	struct liste_esclaves* precedent = NULL;

	while(actuel != NULL) {
		if(actuel->esclave->fd == esclave->fd) {
			// S'il s'agit de la tête de la liste
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

/**
 * Désallouer l'ensemble des esclaves connectés
 */
void desallouer_liste_esclaves() {
	while(memory_data->list_esclaves != NULL) {
		struct liste_esclaves* le = memory_data->list_esclaves->suivant;
		free(memory_data->list_esclaves->esclave);
		free(memory_data->list_esclaves);
		memory_data->list_esclaves = le;
	}
}

/**
 * Afficher les lecteurs d'une liste spécifique
 */
void afficher_lecteurs(struct lecteur* lecteurs, char* s) {
	struct lecteur* actuel = lecteurs;

	printf("[INFOS] Lecteurs %s: [FD] = {", s);
	while(actuel != NULL) {
		printf(" %d", actuel->esclave->fd);
		actuel = actuel->suivant;
	}
	printf(" }\n");
}

/**
 * Vérifie la présence d'un lecteur sur une page
 */
int je_suis_lecteur(struct page* page, struct esclave* esclave) {
	struct lecteur* l = page->lecteurs_actuels;

	while(l != NULL) {
		if(l->esclave->fd == esclave->fd)
			break;
		l = l->suivant;
	}

	return (l != NULL);
}

/**
 * Ajouter un lecteur dans la liste spécifiée si celui-ci n'y figure pas déjà (le paramètre page permet uniquement de différencier la liste des lecteurs actuels du cache)
 */
void _ajouter_lecteur(struct lecteur** lecteurs, struct esclave* esclave, struct page* page) {
	struct lecteur* actuel = *lecteurs;

	// Vérifier la présence du lecteur
	while(actuel != NULL) {
		if(actuel->esclave->fd == esclave->fd)
			break;
		actuel = actuel->suivant;
	}

	// Le lecteur n'y figure pas
	if(actuel == NULL) {
		struct lecteur *nouveau_premier = malloc(sizeof(struct lecteur));
		nouveau_premier->esclave = esclave;
		nouveau_premier->suivant = *lecteurs;

		// L'ajouter en tête
		*lecteurs = nouveau_premier;

		if(page != NULL)
			page->nombre_reader++;
	}
}

/**
 * Ajouter un lecteur dans la liste des lecteurs actuels ET dans le cache de la page spécifiée
 */
void ajouter_lecteur(struct page* page, struct esclave* esclave) {
	_ajouter_lecteur(&page->lecteurs_actuels, esclave, page);
	_ajouter_lecteur(&page->lecteurs_cache, esclave, NULL);
}

/**
 * Supprimer un lecteur spécifique de la liste des lecteurs désirée
 */
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

/**
 * Supprimer un lecteur du cache de la page précisée
 */
void supprimer_lecteur_actuel(struct page* page, struct esclave* esclave) {
	_supprimer_lecteur(&page->lecteurs_actuels, esclave, page);
}

/**
 * Supprimer un lecteur actuel de la page précisée
 */
void supprimer_lecteur_cache(struct page* page, struct esclave* esclave) {
	_supprimer_lecteur(&page->lecteurs_cache, esclave, NULL);
}

/**
 * Désallouer une liste de lecteurs
 */
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

/**
 * Supprimer tous les lecteurs présents dans le cache d'une page spécifique
 */
void supprimer_lecteurs_cache(struct page* page) {
	_desallouer_lecteurs(&page->lecteurs_cache);
}

/**
 * Désallouer toutes les listes de lecteurs d'une page
 */
void desallouer_lecteurs(struct page* page) {
	_desallouer_lecteurs(&page->lecteurs_actuels);
	_desallouer_lecteurs(&page->lecteurs_cache);
}

/**
 * Vérifier la présence d'un esclave spécifique dans le cache des lecteurs d'une page spécifique
 */
int est_dans_cache(struct esclave* esclave, struct page* page) {
	struct lecteur* l = page->lecteurs_cache;	
	while(l != NULL) {
		if(l->esclave->fd == esclave->fd)
			break;
		l = l->suivant;
	}

	return (l != NULL);
}

/**
 * Envoie un message à tous les esclaves qui possèdent la version à jour de la page, quel ne l'est plus
 */
void invalider_page(int fd, struct page* page, int numero) {
	struct lecteur* lecteur = page->lecteurs_cache; 	// Pointeur vers le cache de la page à invalider
	struct message message; 							// Structure de communication avec les esclaves
	int socket_esclave = 0; 							// Socket utilisée pour communiquer avec les esclaves

	printf("[M%d][INVAL] Invalide P%d !\n", fd, numero);

	// Parcourir tous les esclave dans le cache
	while(lecteur != NULL) {
		// Message à envoyer pour invalider les pages
		message.type = REQUETE_INVALIDE_PAGE;
		message.debut_page = numero;

		// Vérifier que l'esclave n'est pas l'ancien écrivain (on ne veut pas invalider la page de la personne qui possède la version à jour de la page)
		if(lecteur->esclave != page->ancient_ecrivain){

			// On ouvre un socket en TCP
			if((socket_esclave = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
				perror("[ERROR][invalider_page] socket");
				exit(1);
			}

			// On se connecte à l'esclave sur son port d'écoute, on réessaie tant que l'on n'a pas été connectés
			printf("[INVAL] Ouverture de connexion avec M%d\n", lecteur->esclave->fd);
			while(connect(socket_esclave, (struct sockaddr*)&lecteur->esclave->info, sizeof(lecteur->esclave->info)) == -1) {
				perror("[ERROR][invalider_page] connect");
				sleep(2);
			}

			// On envoie la requête d'invalidation
			printf("[INVAL] Envoie d'invalidation de P%d à M%d\n", numero, socket_esclave);
			if(send(socket_esclave, &message, sizeof(struct message), 0) == -1) {
				perror("[ERROR][invalider_page] send");
				exit(1);
			}

			// On attend que l'esclave ait fini de traiter l'information avant de fermer la connexion
			if(recv(socket_esclave, &message, sizeof(struct message), 0) == -1) {
				perror("[ERROR][invalider_page] recv");
				exit(1);
			}
			if(message.type != ACK) {
				printf("[ERROR][invalider_page] Problème de synchronisation (%d)\n", message.type);
				exit(1);
			}

			//Fermeture de la connexion
			if(shutdown(socket_esclave, SHUT_RDWR) == -1){
				perror("[ERROR][LoopSlave] shutdown (3)");
				exit(1);
			}	
			
			if(close(socket_esclave)){
				perror("[ERROR][LoopSlave] close (3)");
				exit(1);
			}
		}

		// Passer au lecteur suivant
		lecteur = lecteur->suivant;
	}

	// On vide le cache, plus personne n'a de version à jour de la page
	supprimer_lecteurs_cache(page);
}


/**
 * Demande d'un esclave de verouiller les pages dans le message passé en paramètre.
 * L'esclave en question est aussi passé en paramètre.
 */
void do_lock_read(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre); // Ne pas faire deux actions de lock/unlock en même temps
	struct message requete;							// Variable utilisée pour faire des échanges avec l'esclave
	int dans_cache;									// Variable résultat de la recherche dans la cache

	// Pour toutes les pages demandées, on doit confirmer qu'on peut passer le droit de lecture
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		printf("[M%d][LOCK_READ] Essaie d'accéder à P%d en lecture\n", esclave->fd, i);
		// On attend que l'écrivain ne soit plus sur la page pour donner l'accès en lecture (on donne l'accès que s'il est lui-même écrivain)
		while(memory_data->tab_page[i]->ecrivain != NULL && memory_data->tab_page[i]->ecrivain != esclave) {
			// On redonne le mutex et le thread attend
			pthread_cond_wait(&cond_lecteurs, &memory_data->mutex_maitre);
			i = message.debut_page;
		}
	}

	// Envoyer un message de confirmation à l'esclave (qui était en train d'attendre que les pages soient comfirmées)
	requete.type = ACK;
	if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
		perror("[ERROR][do_lock_read] send (1)");
		exit(1);
	}

	printf("[M%d][LOCK_READ] Peut accéder à(aux) page(s) !\n", esclave->fd);
	// Début de l'envoie des pages en question à l'esclave (ou pas). Une boucle pour chaque page demandé.
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {

		// Vérifie si l'esclave est dans le cache (s'il a demandé la page précédamment). S'il est dedans, il n'a pas besoin de la page, il a deja la version à jours de celle-ci (cache vidée a chaque écriture).
		dans_cache = est_dans_cache(esclave, memory_data->tab_page[i]);
		printf("[~] Est-ce que M%d est dans le cache ? %s\n", esclave->fd, ((dans_cache)?"Oui":"Non"));

		// Donc s'il n'est pas dans le cache et qu'il n'est pas l'écrivain actuel de la page, on lui envoie de la page.
		if(dans_cache == 0 && esclave != memory_data->tab_page[i]->ecrivain){

			// Dans le cas où l'ancien écrivain est vide, cela veux dire que le maître est le propriétaire de la page la plus récente, il lui envoie donc la page lui même
			if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
				printf("[~] Envoie de P%d à M%d\n", i, esclave->fd);

				// On répond à l'esclave en attente, lui disant que le maître va envoyer la page
				requete.type = RETOUR_DEMANDE_PAGE_ENVOIE;
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("[ERROR][do_lock_read] send (2)");
					exit(1);
				}

				// Envoi de la page
				if(send(esclave->fd, memory_data->tab_page[i]->pointer, PAGE_SIZE, 0) == -1) {
					perror("[ERROR][do_lock_read] send (3)");
					exit(1);
				}
			// Le deuxième cas est qu'il y avait un ancien écrivain, ce qui veux dire que le maître n'est pas propriétaire de cette page, il faut rediriger vers un autre esclave
			}else{
				printf("[~] Redirection de M%d vers un esclave (P%d est possédée par M%d)!\n", esclave->fd, i, memory_data->tab_page[i]->ancient_ecrivain->fd);
				
				//On répond l'esclave en attente, lui disant que le maître va redirigée vers l'esclave propriétaire, on envoie ses information (sockaddr_in).
				requete.type = RETOUR_DEMANDE_PAGE_VERS_ESCLAVE;
				requete.esclave_info = memory_data->tab_page[i]->ancient_ecrivain->info;
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("[ERROR][do_lock_read] send (4)");
					exit(1);
				}

				// Le maître attend que l'esclave ait récupéré sa page
				if(recv(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("[ERROR][do_lock_read] recv");
					exit(1);
				}

				// On vérifie que le message reçu est bien la confirmation recherchée
				if(requete.type != ACK){
					printf("[ERROR][do_lock_read] Problème de synchronisation (%d)\n", requete.type);
					exit(1);
				}
			}
		// Dans le cas où l'esclave est dans le cache ou qu'il est l'écrivain actuel... pas besoin d'envoyer, sa page est déjà à jour
		}
		else{
			printf("[~] M%d possède déjà la page à jour...\n", esclave->fd);
		}
		
		// On l'ajoute aux lecteurs actuels (ce qui empêchera les écrivain de venir) et au lecteurs cache (utilisé pour l'invalidation des pages pendant une écriture)
		ajouter_lecteur(memory_data->tab_page[i], esclave);

		// Le maître envoie un message final pour confirmer que lui-même et l'esclave ont bien fait le même ordre d'action avant de passer à la prochaine page à envoyer
		requete.type = ACK;
		if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
			perror("[ERROR][do_lock_read] send (5)");
			exit(1);
		}

		printf("[INFOS] Nombre de lecteurs actuellement sur P%d: %d\n", i, memory_data->tab_page[i]->nombre_reader);
	}

	// On signale tous les lecteurs qui attendent, pour une raison ou une autre, de reprendre leur activité et on rend le mutex
	pthread_cond_signal(&cond_lecteurs);
	pthread_mutex_unlock(&memory_data->mutex_maitre);
}

/**
 * Enlever l'esclave de la liste des lecteurs
 */
void do_unlock_read(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre); //Ne pas faire deux action de lock/unlock en même temps.
	struct message reponse; //Structure pour communiquer avec l'esclave

	// Pour chaque page, on retire cet esclave des lecteurs
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		//Vérifier que c'est bien un lecteur actuellement.
		if(je_suis_lecteur(memory_data->tab_page[i], esclave)) {
			printf("[M%d][UNLOCK_READ] Libère P%d en lecture\n", esclave->fd, i);

			// Retire l'esclave des lecteur de la page
			supprimer_lecteur_actuel(memory_data->tab_page[i], esclave);
			printf("[INFOS] Nombre de lecteurs sur P%d après suppression de M%d: %d\n", i, esclave->fd,  memory_data->tab_page[i]->nombre_reader);
		}
		// S'il ne l'est pas, aucun action n'est prise.
		else {
			printf("[~] M%d essaie d'effectuer une action sans en avoir les droits !\n", esclave->fd);
		}
	}

	reponse.type = ACK;
	//Pour la synchronisation on envoie une message final (Pour faire attendre l'esclave).
	if(send(esclave->fd, &reponse, sizeof(struct message), 0) == -1) {
		printf("[ERROR][do_unlock_read] Problème de synchronisation (%d)\n", reponse.type);
		exit(1);
	}

	// Réveiller les écrivains/lecteurs bloqués. Et rendre le mutex.
	pthread_cond_signal(&cond_lecteurs);
	pthread_cond_signal(&cond_ecrivains);
	pthread_mutex_unlock(&memory_data->mutex_maitre);
}

/**
 * Verouille en écriture d'un nombre de page, l'esclave va devenir propriétaire de ces pages
 */
void do_lock_write(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre);  //Ne pas faire deux action de lock/unlock en même temps.
	struct message requete; //Variable utilisée pour faire des échange avec l'esclave.
	int dans_cache;			//Variable pour vérifier la présence dans le cache.

	//Pour toutes les page demander, on doit confirmée qu'on peut passer le droit d'écriture
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		printf("[M%d][LOCK_WRITE] Essaie d'accéder à P%d en écriture\n", esclave->fd, i);

		//L'esclave voulant écrire doit attendre sur le mutex s'il y a deja un autre écrivain et bien sur que cette écrivain n'est pas lui même. Et que il n'y a aucun lecteurs.
		while( (memory_data->tab_page[i]->ecrivain != NULL && memory_data->tab_page[i]->ecrivain != esclave) || memory_data->tab_page[i]->nombre_reader > 0) {
			pthread_cond_wait(&cond_ecrivains, &memory_data->mutex_maitre);
			i = message.debut_page;
		}
	}

	//Envoyer un message de confirmation à l'esclave (qui été entrain d'attendre que les page soit comfirmé en écriture-> évité problème de synchro).
	requete.type = ACK;
	if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
		perror("write");
		exit(1);
	}
	printf("[M%d][LOCK_WRITE] Peut accéder à(aux) page(s) !\n", esclave->fd);


	//Début de l'envoie des pages en question à l'esclave (ou pas). Une boucle pour chaque page demandé.
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		//Il devient l'écrivain de la page.
		memory_data->tab_page[i]->ecrivain = esclave;
		printf("[INFOS] Nombre de lecteurs sur P%d: %d\n", i, memory_data->tab_page[i]->nombre_reader);

		//Vérifie si l'esclave est dans le cache (s'il a demandé la page précédamment). S'il est dedans, il n'a pas besoin de la page, il a deja la version à jours de celle-ci (cache vidée a chaque écriture).
		dans_cache = est_dans_cache(esclave, memory_data->tab_page[i]);
		//S'il n'est pas dans le cache, et que il n'est pas l'ancien écrivain, on lui envoie la page.
		if(dans_cache == 0 && memory_data->tab_page[i]->ancient_ecrivain != esclave ){
			//Dans le cas où l'ancien écrivain est vide, cela veux dire que le maître est le propriétaire de la page la plus récente, il lui envoie donc la page lui même.
			if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
				printf("[~] Envoie de P%d à M%d\n", i, esclave->fd);

				//On répond l'esclave en attente, lui disant que le maître va envoyer la page (grâce ce type de message).
				requete.type = RETOUR_DEMANDE_PAGE_ENVOIE;
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("[ERROR][do_lock_write] send (1)");
					exit(1);
				}

				//Envoie de la page! (grâce a notre structure on c'est exactement qu'elle partie de la mémoire envoyer)
				if(send(esclave->fd, memory_data->tab_page[i]->pointer, PAGE_SIZE, 0) == -1) {
					perror("[ERROR][do_lock_write] send (2)");
					exit(1);
				}
			//Le deuxième cas est qu'il y avais un ancien écrivain, ce qui veux dire que le maître n'est pas propriétaire de cette page, il faut redirigé vers un autre esclave
			}else{
				printf("[~] Redirection de M%d vers un esclave (Page %d, possédée par M%d)!\n", esclave->fd, i, memory_data->tab_page[i]->ancient_ecrivain->fd);
				
				//On répond l'esclave en attente, lui disant que le maître va redirigée vers l'esclave propriétaire, on envoie ses information (sockaddr_in).
				requete.type = RETOUR_DEMANDE_PAGE_VERS_ESCLAVE;
				requete.esclave_info = memory_data->tab_page[i]->ancient_ecrivain->info; 
				if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("[ERROR][do_lock_write] send (3)");
					exit(1);
				}

				//Le maître attend que l'esclave à récupérer sa page (pour être synchroniser).
				if(recv(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
					perror("[ERROR][do_lock_write] recv");
					exit(1);
				}

				//On vérifie que le message reçu est bien la confirmation rechercher et pas quelque chose de random (Vérification de synchronisation).
				if(requete.type != ACK){
					printf("[ERROR][do_lock_write] Problème de synchronisation (%d)\n", requete.type);
					exit(1);
				}
			}
		//Dans le cas où l'esclave est dans le cache ou qu'il est l'écrivain actuel... pas besoin d'envoyer, sa page est deja à jour.
		}else{
			printf("[~] M%d possède déjà la page à jour...\n", esclave->fd);
		}

		//Le maître envoie un message final pour confirmé que lui même et l'esclave on bien fait le même ordre d'action avant de passer a la prochaine page à envoyer (vérifier la synchronisation).
		requete.type = ACK;
		if(send(esclave->fd, &requete, sizeof(struct message), 0) == -1) {
			printf("[ERROR][do_lock_write] Problème de synchronisation (%d)\n", requete.type);
			exit(1);
		}
	}

	//On rend le mutex (même si casiment aucune actions peuvents être prise pendant qu'il y a un écrivain).
	pthread_mutex_unlock(&memory_data->mutex_maitre);
}

/**
 *	Rend le droit d'écriture, devient le nouveaux propriétaire, et invalide tous les esclave qui possédait l'ancienne version des pages en question
 */
void do_unlock_write(struct message message, struct esclave* esclave) {
	pthread_mutex_lock(&memory_data->mutex_maitre); //Ne pas faire deux action de lock/unlock en même temps.
	struct message reponse; //Variable utilisée pour faire des échange avec l'esclave.

	//Pour chaque page, on retire cette esclave de l'écrivain.
	for(int i=message.debut_page ; i<=message.fin_page ; i++) {
		//Vérifie que l'esclave est bien l'écrivain actuel.
		if(memory_data->tab_page[i]->ecrivain == esclave) {
			// L'esclave est enlever de son role d'écrivain de la page pour que les lecteurs puissent lire de nouveaux (aussi les écrivains).
			memory_data->tab_page[i]->ecrivain = NULL;
			// Egalement il devient l'ancient écrivain de la page. Ce qui veux dire qu'il est propriétaire de la page actuel, tous les nouveaux écrivain/lecteure vont être redirigé vers lui.
			memory_data->tab_page[i]->ancient_ecrivain = esclave;
			
			// On invalide tous les esclave dans le cache(qui avait donc la version a jours de la page). Ce qui veux dire que la prochain fois qui touche a cette page, il devront aller chercher la page de nouveau.
			invalider_page(esclave->fd, memory_data->tab_page[i], i);
			// On l'ajoute au cache pour que quand un prochain écrivain écrit la page, cette écrivain va avoir sa page invalidé également. Il devra aller chercher la nouvelle page.
			_ajouter_lecteur(&memory_data->tab_page[i]->lecteurs_cache, esclave, NULL);

			printf("[M%d][UNLOCK_WRITE] Libère P%d en écriture\n", esclave->fd, i);
		//S'il ne l'est pas, aucun action n'est prise.
		} else {
			printf("[M%d][UNLOCK_WRITE] Essaie d'effectuer une action sans en avoir les droits !\n", esclave->fd);
		}
	}

	//Pour la synchronisation on envoie une message final (Pour faire attendre l'esclave).
	reponse.type = ACK;
	if(send(esclave->fd, &reponse, sizeof(struct message), 0) == -1) {
		printf("[ERROR][do_lock_write] Problème de synchronisation (%d)\n", reponse.type);
		exit(1);
	}

	//Rend le mutex et réveille les écrivains/lecteurs
	pthread_cond_signal(&cond_lecteurs);
	pthread_cond_signal(&cond_ecrivains);
	pthread_mutex_unlock(&memory_data->mutex_maitre);
}

/**
 * Initie le maître, créer la zone mémoire, et initie la structure info sur les pages (memory_data)
 */
void* InitMaster(int size) {
	void* addr_zone;	// Adresse du début de la mémoire partagée
    int modulo; 		// Modulo pour voir si la taille est pile un nombre de page ou un entre-deux

    printf("[~] Lancement du maître...\n");

    // Mapper notre mémoire partagée à la taille donnée en paramètre
	if((addr_zone = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		perror("[EROR][InitMaster] mmap");
		exit(1);
	}

	// Alloue les méta-données (valeurs globales) qui nous servent à retenir diverses informations
	if((memory_data = malloc(sizeof(struct SMEMORY))) == NULL){
		perror("[EROR][InitMaster] malloc");
	}
    memory_data->list_esclaves = NULL; // NULL est la dernière entrée dans la liste chaînée
    memory_data->size = size;
	memory_data->nombre_esclaves_actuel = 0;
	memory_data->nombre_page = size/PAGE_SIZE;
	modulo = size%PAGE_SIZE;

	// On remplie la valeur globale jusqu'à ce que l'on a le nombre de pages que l'on cherche
	for(int i = 0; i <= (size/PAGE_SIZE); i++){
		if((memory_data->tab_page[i] = (struct page *)malloc(sizeof(struct page))) == NULL){
			perror("[EROR][InitMaster] malloc");
		}
		memory_data->tab_page[i]->lecteurs_actuels = NULL;
		memory_data->tab_page[i]->lecteurs_cache = NULL;
		memory_data->tab_page[i]->nombre_reader = 0; 
		memory_data->tab_page[i]->ecrivain = NULL;  //écrivain sur la page, propriétaire
		memory_data->tab_page[i]->ancient_ecrivain = NULL; 
		memory_data->tab_page[i]->pointer = addr_zone+(i*PAGE_SIZE);

		// Initier le surplus, quand on ne remplie pas une page en entière
		if(i == (size/PAGE_SIZE) && modulo != 0){
			memory_data->nombre_page += 1;			
			if((memory_data->tab_page[i+1] = (struct page *)malloc(sizeof(struct page))) == NULL){
				perror("[EROR][InitMaster] malloc");
			}
			memory_data->tab_page[i+1]->lecteurs_actuels = NULL;
			memory_data->tab_page[i+1]->lecteurs_cache = NULL;
			memory_data->tab_page[i+1]->nombre_reader = 0;
			memory_data->tab_page[i+1]->ecrivain = NULL;
			memory_data->tab_page[i+1]->ancient_ecrivain = NULL; 
			memory_data->tab_page[i+1]->pointer = addr_zone+((i+1)*PAGE_SIZE);
			
		}
	}

	printf("[INFOS] Taille de la mémoire partagée: %d octets (%d pages de %d octets)\n", size, memory_data->nombre_page, PAGE_SIZE);

	return addr_zone;
}

/**
 * Enregistrer l'esclave et lui renvoie la taille de la zone mémoire
 */
void do_init(struct message msg, struct esclave* esclave_actuel, int* esclave_end) {
	esclave_actuel->info.sin_port = msg.port;

	ajouter_esclave(esclave_actuel->fd, esclave_actuel->info);

	// Envoi de la taille
	if(send(esclave_actuel->fd, &memory_data->size, sizeof(long), 0) == -1){
		perror("[ERROR][do_init] send");
		*esclave_end = 1;
	}
}

/**
 * L'esclave veut mettre fin à la connexion et se désenregistrer, il doit donc rendre les pages qui lui appartienne
 */
void do_fin(struct esclave* esclave_actuel, int* esclave_end) {
	int nombre_de_page;
	struct message msg;

	// La liste ne doit pas être touchée
	pthread_mutex_lock(&memory_data->mutex_maitre);

	printf("[M%d][TERMI] Terminaison de l'esclave: récupération des pages\n",  esclave_actuel->fd);
	*esclave_end = 1;

	nombre_de_page = 0;
	for(int i = 0 ; i != memory_data->nombre_page ; i++){
		if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
			continue;
		}
		else if(esclave_actuel->fd == memory_data->tab_page[i]->ancient_ecrivain->fd){
			nombre_de_page++;
		}
	}

	if(send(esclave_actuel->fd, &nombre_de_page, sizeof(int), 0) == -1){
		perror("[ERROR][do_fin] send (1)");
		*esclave_end = 1;
	}

	for(int i = 0; i != memory_data->nombre_page; i++){
		if(memory_data->tab_page[i]->ancient_ecrivain == NULL){
			continue;
		}
		else if(esclave_actuel->fd == memory_data->tab_page[i]->ancient_ecrivain->fd){
			memory_data->tab_page[i]->ancient_ecrivain=NULL;

			if(send(esclave_actuel->fd, &i, sizeof(int), 0) == -1){
				perror("[ERROR][do_fin] send (2)");
				*esclave_end = 1;
			}

			if(recv(esclave_actuel->fd, memory_data->tab_page[i]->pointer, PAGE_SIZE, 0) == -1){
				perror("[ERROR][do_fin] recv");
				*esclave_end = 1;
			}
		}
	}

	msg.type = ACK;
	if(send(esclave_actuel->fd, &msg, sizeof(int), 0) == -1){
		perror("[ERROR][do_fin] send (3)");
		*esclave_end = 1;
	}

	struct esclave* esclave = malloc(sizeof(esclave));
	esclave->fd = esclave_actuel->fd;

	for(int i = 0; i != memory_data->nombre_page; i++){
		supprimer_lecteur_cache(memory_data->tab_page[i], esclave);
	}

	supprimer_esclave(esclave);
	pthread_mutex_unlock(&memory_data->mutex_maitre);
}

//Répond à des requête d'un esclave
static void *slaveProcess(void * param){
	// Détacher le thread
	pthread_detach(pthread_self());
	
	struct esclave esclave_actuel = *((struct esclave *)param); // Récupérer les informations de l'esclave pris en charge par le thread
	int esclave_end = 0; 										// Valeur qui signale la fin de la communication
	struct message msg;											// Message reçu par l'esclave

	// Vérifie que InitMaster a bien été appelé
	if(memory_data == NULL){
		printf("[ERROR][slaveProcess] Mémoire non-initialisée !\n");
		exit(1);
	}

	printf("[INIT][M%d] Connexion d'un esclave: prise en charge de l'esclave M%d par un thread\n", esclave_actuel.fd, esclave_actuel.fd);
	while(esclave_end != 1) {
		// Réinitialiser la valeur à chaque boucle
		msg.type = -1;

		// Attendre la réception d'un message
		if(recv(esclave_actuel.fd, &msg, sizeof(struct message),0)== -1){
			perror("[ERROR][InitMaster] recv");
            esclave_end = 1;
		}

		// Effectuer une action en fonction du type de message reçu
		switch (msg.type){
		case REQUETE_INIT:
			do_init(msg, &esclave_actuel, &esclave_end);
			break;
		case REQUETE_LOCK_READ:
			printf("[~] LOCK READ effectué par M%d !\n", esclave_actuel.fd);
			do_lock_read(msg, &esclave_actuel);
			break;
		case REQUETE_LOCK_WRITE:
			printf("[~] LOCK_WRITE effectué par M%d !\n", esclave_actuel.fd);
			do_lock_write(msg, &esclave_actuel);
			break;
		case REQUETE_UNLOCK_READ:
			printf("[~] UNLOCK_READ effectué par M%d !\n", esclave_actuel.fd);
			do_unlock_read(msg, &esclave_actuel);
			break;
		case REQUETE_UNLOCK_WRITE:
			printf("[~] UNLOCK_WRITE effectué par M%d !\n", esclave_actuel.fd);
			do_unlock_write(msg, &esclave_actuel);
			break;
		case REQUETE_FIN:
			do_fin(&esclave_actuel, &esclave_end);
			break;		
		default:
			// Cas anormale, rien à faire
			break;
		}
	}
	printf("[M%d][TERMI] Fin de traitement\n", esclave_actuel.fd);

	afficher_esclaves();

	// Fermer la connexion
	if(shutdown(esclave_actuel.fd, SHUT_RDWR) == -1){
		perror("[ERROR][slaveProcess] shutdown");
		exit(1);
	}
				
	if(close(esclave_actuel.fd)){
		perror("[ERROR][slaveProcess] close");
		exit(1);
	}

	return NULL;
}

/**
 * Boucle principale du maître:
 * Met en place une socket d'écoute pour les esclaves, crée un thread à chaque connexion pour gérer les demandes des esclaves,
 * envoie d'une copie des pages aux esclaves et invalide les copies des esclaves quand une nouvelle page est modifiée
 */
void LoopMaster() {
	printf("[~] Mise en place d'une socket d'écoute\n");

	int socketEcoutefd, socketEsclaveFD;
	struct esclave param;
	struct sockaddr_in addrclt2;
	socklen_t sz = sizeof(addrclt2);
	struct sockaddr_in addrclt;
	pthread_t th;
	
	// Mise en place d'un mutex
	pthread_mutex_init(&memory_data->mutex_maitre, NULL);
        
	// Vérifier si InitMaster à bien été appelé
	if(memory_data == NULL){
		printf("[ERROR][LoopMaster] Memoire non-initialisée !\n");
		exit(1);
	}

	addrclt.sin_family = AF_INET;
	addrclt.sin_port = htons(PORT_MAITRE);
	addrclt.sin_addr.s_addr = INADDR_ANY;

	// Ouverture du socket
	if((socketEcoutefd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("[ERROR][LoopMaster] socket");
		exit(1);
	}

	// Lier le port au socket
	if(bind(socketEcoutefd, (struct sockaddr*)&addrclt, sizeof(addrclt)) == -1) {
		perror("[ERROR][LoopMaster] bind");
		exit(2);
	}

	// Ecoute sur le port
	if(listen(socketEcoutefd, 10) == -1) {
		perror("[ERROR][LoopMaster] listen");
		exit(3);
	}
	
	printf("[OK] Prêt à accepter des connexions !\n");

	while(1){
		// Si une connexion est validée et que le nombre d'esclaves connectés n'est pas au maximun
		if((socketEsclaveFD = accept(socketEcoutefd, (struct sockaddr*)&addrclt2, &sz)) != -1) {

			// Mettre en place les paramètres pour la gestion de l'esclave
			param.fd = socketEsclaveFD;
			param.info = addrclt2;

			// Thread pour la gestion du nouvel esclave
			pthread_create(&th, NULL, slaveProcess, (void *)&param);	
		}
	}	
	
	// Fermeture du socket
	if(shutdown(socketEcoutefd, SHUT_RDWR) == -1){
		perror("[ERROR][LoopMaster] shutdown");
	}
				
	if(close(socketEcoutefd)){
		perror("[ERROR][LoopMaster] close");
		exit(1);
	}
}

/**
 * Libère la mémoire partagée et les méta-donnés des pages
 */
void endMaster(void* data, int size){

	// Vérifie si InitMaster à bien été appeler
	if(memory_data == NULL){
		perror("[ERROR][endMaster] Mémoire non-initialisée");
		exit(1);
	}

	// Libérer la mémoire partagée
	if(munmap(data, size) == -1){		
		perror("Erreur unmmap!");
	}
	
	// Désallouer la liste des esclaves actuellement connectés
	desallouer_liste_esclaves();

	// Libèrer la mémoire de chaque struct page dans la struct SMEMORY
	for(int i = 0; i <= memory_data->nombre_page; i++){
		free(memory_data->tab_page[i]);
	}

	// Libérer la mémoire que prend la struct MEMORY
	free(memory_data);

	printf("[OK] Terminaison du maître ! A bientôt !\n");
}
