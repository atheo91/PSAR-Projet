/***** LIBRAIRIES *****/

#include <netinet/ip.h>


/***** CONSTANTES *****/

#define TAB_SIZE							9217
#define NB_PAGE_MAX 						1024
#define PAGE_SIZE 							4096
#define PORT_MAITRE 						10098

#define REQUETE_INIT 						1
#define REQUETE_DEMANDE_PAGE 				2
#define REQUETE_LOCK_READ 					3
#define REQUETE_UNLOCK_READ 				4
#define REQUETE_LOCK_WRITE 					5
#define REQUETE_UNLOCK_WRITE				6
#define RETOUR_DEMANDE_PAGE_ENVOIE 			7
#define RETOUR_DEMANDE_PAGE_VERS_ESCLAVE 	8
#define REQUETE_INVALIDE_PAGE 				9
#define ACK									100
#define REQUETE_FIN 						999
#define NONE								0


/***** STRUCTURES *****/

// Donnée répartie example
struct data {
	int id;
	int data[TAB_SIZE];
};

// --- STRUCTURES MAÎTRE ---

// Informations sur chaque page
struct page {
	void * pointer;						// adresse sur le début de page
	int nombre_reader;					// nombre de lecteurs sur cette page
	struct lecteur* lecteurs_actuels;	// liste des lecteurs actuellement en train de lire sur cette page
	struct lecteur* lecteurs_cache;		// liste des esclaves ayant effectué une lecture depuis la dernière écriture sur cette page
	struct esclave* ecrivain;			// s'il y a un écrivain actuellement sur cette page
	struct esclave* ancient_ecrivain;   // le dernier à avoir écrit sur la page
};

// Liste des esclaves actuellement connectés
struct liste_esclaves {
	struct esclave* esclave;
	struct liste_esclaves* suivant;
};

// Liste chaînée représentant des lecteurs
struct lecteur {
	struct esclave* esclave;
	struct lecteur* suivant;
};

// Informations sur chaque esclave
struct esclave {
	int fd;						// Descripteur de fichier de l'esclave
	struct sockaddr_in info;	// IP, Port, ...
};

// Données de la mémoire partagée
struct SMEMORY {
	int nombre_page;						// Nombre de pages à gérer
	int nombre_esclaves_actuel;				// Nombre d'esclaves actuellement connectés
    int size;								//
	pthread_mutex_t mutex_maitre;			// Mutex permettant la cohérence des données partagées entre les threads
	struct liste_esclaves * list_esclaves;	// Liste contenant les esclaves actuellement connectés
	struct page * tab_page[NB_PAGE_MAX];	// Ensemble des pages (l'index représente le numéro de page)
};

// --- STRUCTURES ESCLAVE ---

// Donnée des droits sur les pages dans la zone mémoire
struct memoire_pages{
	int num_page;					// Numéro de la page concernée
	int validite;					// Vérifier la validité de la page (que les dernières informations lues ne soient pas erronées)
	int droit_write;				// Droit en écriture de l'esclave sur la page
	int droit_read;					// Droit en lecture de l'esclave sur la page
	struct memoire_pages * next;
};

// Paramètre pour le thread gérant le userfaultfd
struct handle_params {
    int uffd;						// Descripteur de fichier du userfaultfd
	int nombre_page;				// Nombre de pages
	void * pointeurZoneMemoire;		// Zone mémoire gérée par le userfaultfd
};

// Message envoyé entre le maître et ses esclaves
struct message {
	int type;						// Type du message: LOCK_READ, UNLOCK_READ, ...
	int debut_page;					// [LOCK,UNLOCK] le numéro de page de la donnée qu'on cherche à récupérer (par rapport au début de l'adresse)
	int fin_page;					// [LOCK,UNLOCK] le numéro de page de la donnée qu'on cherche à récupérer (par rapport à la fin de l'adresse)
	struct sockaddr_in esclave_info;// [LOCK,UNLOCK] esclave à contacter en cas d'invalidité des pages
	int port;						// [INIT] port sur lequel l'esclave écoute
};


/***** SIGNATURE DES FONCTIONS *****/

// --- PARTIE MAITRE ---
void *InitMaster(int size);

void LoopMaster();

void endMaster(void * data, int size);

void afficher_esclaves();

struct esclave* ajouter_esclave(int fd, struct sockaddr_in adresse);

void supprimer_esclave(struct esclave* esclave);

void desallouer_liste_esclaves();

void afficher_lecteurs(struct lecteur* lecteurs, char* s);

int je_suis_lecteur(struct page* page, struct esclave* esclave);

void _ajouter_lecteur(struct lecteur** lecteurs, struct esclave* esclave, struct page* page);

void ajouter_lecteur(struct page* page, struct esclave* esclave);

void _supprimer_lecteur(struct lecteur** lecteurs, struct esclave* esclave, struct page* page);

void supprimer_lecteur_actuel(struct page* page, struct esclave* esclave);

void supprimer_lecteur_cache(struct page* page, struct esclave* esclave);

void _desallouer_lecteurs(struct lecteur** lecteurs);

void supprimer_lecteurs_cache(struct page* page);

void desallouer_lecteurs(struct page* page);

int est_dans_cache(struct esclave* esclave, struct page* page);

void invalider_page(int fd, struct page* page, int numero);

void do_lock_read(struct message message, struct esclave* esclave);

void do_unlock_read(struct message message, struct esclave* esclave);

void do_lock_write(struct message message, struct esclave* esclave);

void do_unlock_write(struct message message, struct esclave* esclave);

void do_init(struct message msg, struct esclave *esclave_actuel, int* esclave_end);

void do_fin(struct esclave* esclave_actuel, int* esclave_end);

static void *slaveProcess(void * param);

// --- PARTIE ESCLAVE ---
void *InitSlave(char *HostMaster);

void lock_read(void *adr, int s);

void unlock_read(void *adr, int s);

void lock_write(void *adr, int s);

void unlock_write(void *adr, int s);

void endSlave(void * data, int size);

void* LoopSlave(void * adresse);

static void *handle_defaut(void * arg);

unsigned long get_taille_memoire(int connexion_FD);

void set_connection(char* HostMaster, int* port, long* taille_recv);

void set_userfaultfd(int num_page);

long trouver_numero_page(void* data);
