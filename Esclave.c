// TCP -> Client: socket, connect, send/recv, shutdown, close

#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/mman.h>
#include <fcntl.h>
#define PORT 10000

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
	printf("a=%d\n", *a);

	// CREATION SOCKET
	int socketfd;

	if((socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("socket");
		exit(1);
	}

	struct addrinfo* res;

	if(getaddrinfo("localhost", NULL, NULL, &res) != 0) {
		perror("getaddrinfo");
		exit(2);
	}

	struct in_addr ipv4 = ((struct sockaddr_in*) res->ai_addr)->sin_addr;

	freeaddrinfo(res);

	struct sockaddr_in addr;
	memset((void*)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr = ipv4;

	if(connect(socketfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("connect");
		exit(3);
	}

	// READ/WRITE
	char* b = "bonjour";
	*a = 5;
	//
	write(socketfd, b, strlen(b)+1);
	//

	// FERMETURE SOCKET
	shutdown(socketfd, SHUT_RDWR);
	close(socketfd);

	return 0;
}
