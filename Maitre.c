// TCP : S -> socket, bind, listen, accept, send/recv, shutdown, close
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#define PORT 10000
#define NB_CLIENTS 10
#define SIZE 128

int main(int argc, char** argv) {

	// CREATION MEMOIRE PARTAGEE
	int fd;
	if((fd = shm_open("/memoire", O_CREAT | O_RDWR, 0600)) < 0) {
		perror("shm_open");
		exit(5);
	}

	ftruncate(fd, sizeof(int));

	void* adresse;
	if((adresse = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		perror("mmap");
		exit(6);
	}

	// TEST ICI
	int* a;

	a = adresse;

	*a = 10;

	// CREATION SOCKET
	int socketfd, socketClientFD;

	struct sockaddr_in addrclt;
	addrclt.sin_family = AF_INET;
	addrclt.sin_port = htons(PORT);
	addrclt.sin_addr.s_addr = INADDR_ANY;

	struct sockaddr_in addrclt2;
	socklen_t sz = sizeof(addrclt2);

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

	if((socketClientFD = accept(socketfd, (struct sockaddr*)&addrclt2, &sz)) == -1) {
		perror("accept");
		exit(4);
	}

	// READ/WRITE
	char* b = malloc(sizeof(char)*SIZE);

	read(socketClientFD, b, SIZE);
	printf("Reçu: %s\n", b);

	//
	printf("a=%d\n", *a);
	//

	// FERMETURE SOCKET TCP
	shutdown(socketfd, SHUT_RDWR);
	close(socketfd);

	// MEMOIRE PARTAGEE
	munmap(adresse, sizeof(int));
	close(fd);
	shm_unlink("/memoire");

	return 0;
}
