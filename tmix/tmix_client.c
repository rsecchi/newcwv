#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>

#define MAX_TRSF 20000
#define SERV_PORT 7001
#define HDR_SZ  sizeof(uint32_t)

#define min(x,y)   ((x)<(y))?(x):(y)

int id;
struct hostent *server;

void error(const char *msg)
{
	perror(msg);
	exit(0);
}

struct Object{
	int request_size;
	int response_size;
	struct timespec blocking_time;
	struct Object * next;

	// statistics
	struct timeval dur;

};
typedef struct Object obj_item;


struct MyConnection {
	int id;
	int sockfd;
	struct timespec start_time;
	obj_item *obj_head;
	struct  MyConnection * next;
	struct timeval time;
};

typedef struct MyConnection con_item;
con_item *head;	//this the starting point of the whole data structure


/* Data Structure
*	 	-----------		 -----------
*  head	--->>>	|Connection1|	--->>>	|Connection2|		...
*		 -----------		 -----------
*	 	     |			      |
*	   	  Object1		   Object1
*	  	     |			      |
*		  Object2  		   Object2
*	   	    ...		     	     ...
*	      
*/


void send_burst(int sockfd, char *buffer, int size)
{
	int sent, n; 
	
	for (sent = 0; sent < size; sent += n) {

		n = write(sockfd, buffer, min(size-sent, MAX_TRSF));
		if (n < 0) {
			error("ERROR writing to socket");
			exit(1);
		}

	}

}

struct timeval single_req_resp_cycle (int sockfd, int req_size, 
int res_size, struct timespec ts)
{
	struct timeval ts1, ts2, ts_diff;
        char buffer[MAX_TRSF];
	int byterx, tmp, size;
	bzero(buffer, MAX_TRSF);

	/* Builds the header */
	tmp = htonl(req_size);
        memcpy(buffer, &tmp, sizeof(int));
	tmp = htonl(res_size);
        memcpy(buffer+sizeof(int), &tmp, sizeof(int));
        

        size = req_size + 2*sizeof(uint32_t);

	/* timestamp before request */
	gettimeofday(&ts1, NULL);

	/* Client send request */
        send_burst(sockfd, buffer, size);

	/* Client read response */
	for(byterx=0; byterx<res_size;) 
		byterx += read(sockfd, buffer, MAX_TRSF);
	

	/* timestamp after request */
	gettimeofday(&ts2, NULL);
	timersub(&ts2, &ts1, &ts_diff);

        nanosleep(&ts, NULL);

	return ts_diff;
}

void split_string(char *input, char *token, char *output[]){
	char *tmp;
	int i=0;
	
	tmp = strtok(input, token);
        while (tmp != NULL) {
            //printf("tokens: %s\n", tmp);
            output[i++] = tmp;
            tmp = strtok(NULL, " \n");
        
	}
}

void parse_cvec_file (char *filename)
{

    //<request size> <response size> <delay>
    char line[100];
    char *str[3];
    double delay_s;
    
    con_item *cur_con_pt = NULL, *new_con_pt = NULL;    // MyConnection Pointers
    obj_item *cur_obj_pt = NULL, *new_obj_pt = NULL;    // Object pointers
    
    FILE *fp;
    fp = fopen (filename,"r");
    if (fp==NULL){
        fprintf(stderr, "could not open file");
        exit(0);
    }
    //printf ("using fgets \n");
    
    while (fgets (line, sizeof (line), fp) != NULL){

	//printf("line: %s", line);
	if ( !strcmp (line,"\n"))
		continue;
	
	split_string(line," \n",str);
		
	if ( !strcmp(str[0],"C") ){
		/*	create a new MyConnection item which will hold the list of objects
		 *	and its own start time
		 */
		new_con_pt = (con_item *) malloc (sizeof (con_item));
		new_con_pt->id = id++;
		new_con_pt->time.tv_sec = 0;
		new_con_pt->time.tv_usec = 0;
		new_con_pt->next = NULL;
		if (cur_con_pt != NULL)
			cur_con_pt->next = new_con_pt;
		cur_con_pt = new_con_pt;
		if (head == NULL)
			head = cur_con_pt;
		
		delay_s = atof(str[1]);	
		cur_con_pt->start_time.tv_sec = (int) delay_s;
		cur_con_pt->start_time.tv_nsec = (long) ( (delay_s - (int)delay_s)*1000000000 );
		cur_con_pt->obj_head = NULL;
		cur_obj_pt = NULL;
	}
	
	else {
		/* 	add Object item with request size, response size and block time
		 *	to the list of object to the current MyConnection item
		 */
		new_obj_pt = (obj_item *) malloc (sizeof (obj_item));
		new_obj_pt->next = NULL;
		if (cur_obj_pt != NULL)
			cur_obj_pt->next = new_obj_pt;
		cur_obj_pt = new_obj_pt;
		if (cur_con_pt->obj_head == NULL)
			cur_con_pt->obj_head = cur_obj_pt;
			
		cur_obj_pt->request_size = atoi (str[0]);
		cur_obj_pt->response_size = atoi (str[1]);
		delay_s = atof(str[2]);
		cur_obj_pt->blocking_time.tv_sec = (int) delay_s;
		cur_obj_pt->blocking_time.tv_nsec = (long) ( ( delay_s - (int)delay_s) *1000000000 );

		cur_obj_pt->dur.tv_sec = cur_obj_pt->dur.tv_usec = 0;
	}
	
    } // end of fgets while
    
    
}


void parse_cmdline(int argc, char *argv[])
{
        if (argc < 3){
               fprintf(stderr, "usage: %s <hostname> <cvec_file_name>\n", argv[0]);
		exit(1);
        }
        
        if (!(server = gethostbyname(argv[1]))) {
		fprintf(stderr, "ERROR, no such host\n");
		exit(1);
        }
    
        parse_cvec_file (argv[2]);    
}


void list_print()
{

	con_item *ptr1 = head;;
	obj_item *ptr2; ;

	while(ptr1 != NULL) {
		ptr2 = ptr1->obj_head;

		printf("C %lu.%09lu <%d> :%d.%06d\n",
				ptr1->start_time.tv_sec,
				ptr1->start_time.tv_nsec, 
				ptr1->id,
				(int)ptr1->time.tv_sec,
				(int)ptr1->time.tv_usec);

		while (ptr2 != NULL) {
			printf("%d %d %ld.%09ld : %d.%06d\n", 
				ptr2->request_size, 
				ptr2->response_size, 
				ptr2->blocking_time.tv_sec, 
				ptr2->blocking_time.tv_nsec,
				(int)ptr2->dur.tv_sec,
				(int)ptr2->dur.tv_usec);
			ptr2= ptr2->next;
		}

		printf("\n");
		ptr1 = ptr1->next;
	}

}

void *start_connection(void *p)
{
	struct sockaddr_in serv_addr;
	con_item *mc = p;
	obj_item *obj;
	struct timeval ts, te;
	
	mc->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (mc->sockfd < 0)
		error("ERROR opening socket");

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,
	      (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(SERV_PORT);

	nanosleep(&(mc->start_time), NULL);

	printf("start_connection: A %d\n",mc->id); 

	if( connect(mc->sockfd, (struct sockaddr *)&serv_addr,
		       sizeof(serv_addr))){
		error("ERROR connecting");
		exit(0);
	}

	printf("start_connection: B %d\n",mc->id); 
	gettimeofday(&ts, NULL);
	obj = mc->obj_head;	
	while(obj != NULL) {
		obj->dur = single_req_resp_cycle(
				mc->sockfd,
				obj->request_size,
				obj->response_size, 
				obj->blocking_time);

		obj = obj->next;
	}
	printf("start_connection: C %d\n",mc->id); 
	gettimeofday(&te, NULL);
	timersub(&te, &ts, &(mc->time));

	close(mc->sockfd);
	return 0;
}

int no_of_connections()
{

	con_item *tmp = head;
	int i=0;
	while (tmp != NULL){
		i++;
		tmp = tmp->next;
	}
	return i;			
}

int main(int argc, char *argv[])
{
        con_item *tmp;
	int i, NC;
	int myerr;	
	pthread_attr_t conn_attr;
	
        parse_cmdline(argc, argv);

	NC = no_of_connections();
	printf("nc: %d\n", NC);
	
	pthread_t con_thread[NC];

	pthread_attr_init(&conn_attr);
	pthread_attr_setstacksize(&conn_attr, 65536);
	
	tmp=head;
	for (i=0; i<NC ; i++){
		if ((myerr=pthread_create(&con_thread[i], &conn_attr, start_connection, tmp)) != 0) {
			fprintf(stderr, "%d ERROR!!!\n",myerr);
			exit(1);
		}
		printf("%d Created thread for %d\n", i, tmp->obj_head->request_size);
		tmp = tmp->next;
	}
	
	for (i=0; i<NC; i++) {
		pthread_join(con_thread[i], NULL);
		printf("Thread %d joined\n",i);
	}

	list_print();

	return 0;

}

