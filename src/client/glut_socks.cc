/* TCP/IP communication for GLUT based programs
 * Server (GLUT) side. 
 *
 * Alexey Zhelezov, DESY/ITEP, 2005 
 * July 2005, Jörgen Samson: small fix to keep
 *            TCP/IP connection alive if data
 *            is temporary not available
 */

char trusted_hosts[50]; 

#include "ced.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>

#include <errno.h>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

 /* Version 2 refactor changes:
  * Removed __glutSockets linked list
  * Removed add_socket and delete_socket
  * Created tcp_server_accept
 */ 

static int _server_fd = -1;
static void (*_pending_user_func)(void *) = nullptr; // user callback registered via tcp_server()

int socket_fd = -1; // active fd: server socket while waiting, client socket once connected
void (*socket_fn)(void) = nullptr; // swaps between tcp_server_accept and tcp_server_read

bool client_connected=false;

static void tcp_server_accept(void); // forward declaration

static void tcp_server_read(void){
  static unsigned char *buf=0;
  static unsigned buf_size=0;
  unsigned got_sum;
  int size,need_size=0;

  if(buf_size<8){
    buf=(unsigned char *)(realloc(buf,8));
    buf_size=8;
  }
  if((size=read(socket_fd,buf,8))){
    need_size=*((unsigned *)buf);
    //if((need_size >=8) && (need_size<10000000)){
    if((need_size >=8)) { //&& (need_size<1000000000)){
      if(need_size>8){
	if((unsigned)need_size>buf_size){
	  buf=(unsigned char *) realloc(buf,need_size);
	  buf_size=need_size;
	}
	got_sum=8;
	while(got_sum<(unsigned)need_size){
	    size=read(socket_fd,buf+got_sum,need_size-got_sum);
	    if(size <= 0){
	      if (errno == EAGAIN)
		continue; // keep trying..
	      perror("In glut_socks::tcp_server_read");
	      need_size=0; // problem
	      break;
	    }
	    got_sum+=size;
	}
      }
    } else
      need_size=0;
  }
  if(need_size<8){
    fprintf(stderr,"INFO: client is disconnected\n");
    close(socket_fd);
    client_connected=false;
    socket_fd=_server_fd;
    socket_fn=tcp_server_accept;
    return;
  }
  _pending_user_func(buf);
}

static void tcp_server_accept(void){
  int fd;
  struct sockaddr_in myclient;
  unsigned int size=sizeof(myclient);

  //fd=accept(_server_fd,0,0);
  fd=accept(_server_fd,(struct sockaddr *)&myclient, &size);
  //fd=accept(list->fd, &myclient, &size);

  //printf("New connection from: %s\n", inet_ntoa(myclient.sin_addr));

  //printf("trusted hosts: %s\n",trusted_hosts); 
  if(strcmp(inet_ntoa(myclient.sin_addr), "127.0.0.1")){ //not an connection from localhost
    if(strcmp(inet_ntoa(myclient.sin_addr), trusted_hosts)){
        struct hostent *hp;
        in_addr_t data=inet_addr(inet_ntoa(myclient.sin_addr));
        hp = gethostbyaddr(&data, 4, AF_INET);
        if(hp == NULL){ 
            printf("CED: Reject remote connection from: UnknownHost (%s)\n", inet_ntoa(myclient.sin_addr)); 
        }
        else{ 
            printf("CED: Reject remote connection from: %s (%s)\n", hp->h_name, inet_ntoa(myclient.sin_addr));
        }

        close(fd);
        return;
    }
    printf("CED: Accepted trusted connection from ip %s\n", inet_ntoa(myclient.sin_addr));
    client_connected=true;

  }else{
      printf("CED: Accepted connection from localhost\n");
      client_connected=true;
  }

  if(fd<0){
    perror("WARNING: can't accept connection");
    return;
  }

  socket_fd=fd;
  socket_fn=tcp_server_read;
  fprintf(stderr,"INFO: new client - socketID: %d\n", fd);
  _pending_user_func(0);
}  

/* API */
int tcp_server(unsigned short port,
		    void (*user_func)(void *data)){
  int fd=socket(PF_INET,SOCK_STREAM,0);
  struct sockaddr_in addr;
  int one=1;
  long flags;

#ifdef __APPLE__
  setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
#else
  setsockopt(fd,SOL_TCP,TCP_NODELAY,&one,sizeof(one));  
#endif    

  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
  flags=fcntl(fd,F_GETFL);
  flags|=O_NONBLOCK;
  fcntl(fd,F_SETFL,flags);

  addr.sin_family=AF_INET;
  addr.sin_port=htons(port);
  addr.sin_addr.s_addr=INADDR_ANY;
  if(bind(fd,(struct sockaddr *)&addr,sizeof(addr))){
    perror("ERROR: can't bind to server port");
    close(fd);
    return -1;
  }
  if(listen(fd,10)){
    perror("ERROR: can't listen on server port");
    close(fd);
    return -1;
  }
  _server_fd=fd;
  _pending_user_func=user_func;
  socket_fd=_server_fd;
  socket_fn=tcp_server_accept;
  return 0;
}
