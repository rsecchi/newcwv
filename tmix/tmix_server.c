#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <netinet/tcp.h>

#define min(x,y)   ((x)<(y))?(x):(y)
#define MAX_TRSF 20000
#define PORT 7001
#define HDR_SZ  sizeof(uint32_t)

int sockfd;
sem_t mutex;
struct sockaddr_in serv_addr, cli_addr;

void error(const char *msg)
{
	perror(msg);
	exit(1);
}

void bind_socket(int port_no)
{
	int opt_nodelay = 1;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, 
			(void*)&opt_nodelay, 
			sizeof(opt_nodelay));
	//setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *)&win, sizeof(win));
	if (sockfd < 0)
		error("ERROR opening socket");
        
	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port_no);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR on binding");
}

void send_burst(int sock, char* buffer, int size)
{
	int sent, n;

	for (sent = 0; sent < size; sent += n) {

		n = write(sock, buffer, min(size-sent,MAX_TRSF));
		if (n < 0) {
			fprintf(stderr, "write interrupted, bail out\n");
			return;
		}
	
	}
	printf("Burst sent: %d B\n", sent);

}


int receive_and_send(int sock) 
{
        char buffer[MAX_TRSF];
        
        uint32_t n=0, datalen, resp_size;
	bzero(buffer, MAX_TRSF);

	/* Reads the header */
	n = read(sock, buffer, 2*sizeof(uint32_t));
	if (!n)
		return 0;

	if ( n < 2*sizeof(uint32_t) ) {
		fprintf(stderr, "server: bad header received\n");
		return 0;
	}

	datalen = ntohl(*(uint32_t*)buffer);
	resp_size = ntohl(*(uint32_t*)(buffer+sizeof(int)));
	printf("Request size %d B\n", datalen);
	printf("Sending Response: %d B\n", resp_size);

	/* reads the request */
	for(n=0; n<datalen; )
		n += read(sock, buffer, MAX_TRSF);
 
	/* sending part */
	send_burst(sock, buffer, resp_size);
                
	printf("DONE req: %d  resp: %d\n", n, resp_size);
        return 1;
}

void* single_exchange(void* nt)
{
	int sock = *(int*)nt;
	sem_post(&mutex);

	while(receive_and_send(sock));
       
        close(sock);
	pthread_exit(0);
}

int main(int argc, char *argv[])
{
        socklen_t clilen;
	pthread_t ntp;
       	int fd;
	
	signal(SIGPIPE, SIG_IGN); 
	bind_socket(PORT);
	listen(sockfd, 5);
       
	sem_init(&mutex, 1, 1);
 
        clilen = sizeof(cli_addr);
	do {
		sem_wait(&mutex);
	        fd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);

		if (fd < 0) {
			error("ERROR on accept");
			exit(1);
		}

		pthread_create(&ntp, NULL, single_exchange, &fd);
		pthread_detach(ntp);

	} while(1);
        
	close(sockfd);
}

