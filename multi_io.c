#·········································网络io，io多路复用（select、poll、epoll）·········································


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h> 
#include <sys/time.h>
#include <poll.h>


#define BUFFER_LEN 1024	
#define POLL_SIZE 1024

void *client_thread(void *arg){
	int clientfd=*(int *)arg;

	while(1){
		char buffer[BUFFER_LEN]={0};
		int ret = recv(clientfd,buffer,BUFFER_LEN,0);
		if (ret == 0){
			//客户端断开
			close(clientfd);
			break;
		}

		printf("ret: %d,buffer:%s\n", ret,buffer);

		send(clientfd,buffer,ret,0);
	}
}

int main(){
	int sockfd=socket(AF_INET,SOCK_STREAM,0);

	struct sockaddr_in servaddr;
	memset(&servaddr,0,sizeof(struct sockaddr_in));
	servaddr.sin_family=AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(9999);

	if (-1 == bind(sockfd,(struct sockaddr *)&servaddr,sizeof(struct sockaddr))){
		printf("bind failed: %s",strerror(errno));
		return -1;
	}

	listen(sockfd,10);

	struct sockaddr_in clientaddr;
	memset(&clientaddr,0,sizeof(struct sockaddr_in));
	socklen_t len=sizeof(struct sockaddr);
#if 0
	// ·································简单的IO请求········································
	// 设置为非阻塞
	// int flags=fcntl(sockfd,F_GETFL,0);
	// flags |= O_NONBLOCK;
	// fcntl(sockfd,F_SETFL,flags);

	int clientfd=accept(sockfd,(struct sockaddr*)&clientaddr,&len); //调用 accept() 函数后，它会一直阻塞等待直到有新的客户端连接请求到达为止。

	printf("accept\n");

	while (1){
		char buffer[BUFFER_LEN]={0};
		int ret=recv(clientfd,buffer,BUFFER_LEN,0);
		printf("ret: %d,buffer:%s\n", ret,buffer);
		send(clientfd,buffer,ret,0);
			
	}

#elif 0
	// ·································一请求一线程········································
	while(1){
		int clientfd=accept(sockfd,(struct sockaddr *)&clientaddr,&len);

		printf("accept\n");
		
		pthread_t thread_id;
		pthread_create(&thread_id,NULL,client_thread,&clientfd);
	}


#elif 0	
	// ·································select 函数用法········································
	fd_set rfds,rset;	// 创建并初始化一个文件描述符集合
	FD_ZERO(&rfds);	// 初始化为空集
	FD_SET(sockfd,&rfds);	// 将sockfd添加到rfds集合中


	int maxfd=sockfd;
	int clientfd=0;
	while(1){
		rset=rfds;
		//监听文件描述符集合，并等待有可读事件(包括连接和发送)发生
		int nready=select(maxfd+1,&rset,NULL,NULL,NULL);
		printf("nready: %d\n",nready);
		/*判断监听套接字sockfd上是否有可读事件的。如果存在可读事件，
		说明有新的客户端连接请求到达了服务器，需要调用accept函数接受连接,调用FD_SET将客户端套接字clientfd加入到文件描述符集合rfds中。*/
		if(FD_ISSET(sockfd,&rset)){ 
			clientfd=accept(sockfd,(struct sockaddr *)&clientaddr,&len);
			printf("accept: %d\n",clientfd);

			FD_SET(clientfd,&rfds);
			if (clientfd > maxfd) maxfd=clientfd;
			
			if (--nready == 0) continue;
		}

		int i=0;
		for (i=sockfd+1;i<=maxfd;i++){
			if (FD_ISSET(i,&rset)){
				char buffer[BUFFER_LEN]={0};
				int ret = recv(clientfd,buffer,BUFFER_LEN,0);
				if (ret == 0){
					//客户端断开
					close(clientfd);
					break;
				}

				printf("ret: %d,buffer:%s\n", ret,buffer);

				send(clientfd,buffer,ret,0);
			}
		}

	}
#elif 0
	// ·································poll 函数用法········································
	struct pollfd fds[POLL_SIZE] = {0};
	fds[sockfd].fd = sockfd;
	fds[sockfd].events = POLLIN;

	int maxfd=sockfd;
	int clientfd=0;

	while(1){
		int nready = poll(fds,maxfd+1,-1);
		if(fds[sockfd].revents & POLLIN){

			clientfd=accept(sockfd,(struct sockaddr *)&clientaddr,&len);
			printf("accrpt: %d\n",clientfd);

			fds[clientfd].fd=clientfd;
			fds[clientfd].events=POLLIN;;

			if (clientfd > maxfd) maxfd=clientfd;

			if(--nready == 0) continue;
		}

		int i=0;
		for (i=sockfd+1;i<=maxfd;i++){
			if(fds[i].revents & POLLIN){

				char buffer[BUFFER_LEN]={0};
				int ret=recv(clientfd,buffer,BUFFER_LEN,0);
				if (ret == 0){
					fds[clientfd].fd=-1;
					fds[clientfd].events=0;

					close(clientfd);
					break;
				}

				printf("ret: %d,buffer:%s\n", ret,buffer);

				send(clientfd,buffer,ret,0);
			}
		}
	}
#elif 1
	// ·································epoll 函数用法········································
	int epfd = epoll_create(1);

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = sockfd;

	epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);

	struct epoll_event events[1024]={0};

	while (1){
		int nready = epoll_wait(epfd, events, 1024, -1);
		if (nready < 0) continue;

		int i=0;
		for (i=0;i<nready;i++){
			int connectfd = events[i].data.fd;
			if (connectfd == sockfd){
				int clientfd = accept(sockfd,(struct sockaddr *)&clientaddr,&len);
				if (clientfd <= 0) continue;;

				printf(" clientfd:%d\n",clientfd);
				ev.events=EPOLLIN;
				ev.data.fd=clientfd;
				epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd ,&ev );
			}
			else if (events[i].events & EPOLLIN ){
				char buffer[BUFFER_LEN] = {0};
				int ret = recv(connectfd, buffer, BUFFER_LEN, 0);
				if (ret > 0){
					printf("recv: %s\n", buffer);
					send(connectfd, buffer, ret, 0);
				}
				else if(ret == 0){
					printf("close\n");
					epoll_ctl(epfd, EPOLL_CTL_DEL, connectfd ,NULL );
					close(connectfd);
				}
			}
		}
	}
#elif 0
	// ·································epoll EPOLLET函数用法········································
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

	int epfd = epoll_create(1);

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = sockfd;

	epoll_ctl(epfd,EPOLL_CTL_ADD,sockfd,&ev);

	struct epoll_event events[1024]={0};

	while (1){
		int nready = epoll_wait(epfd, events, 1024, -1);
		if (nready < 0) continue;

		int i=0;
		for (i=0;i<nready;i++){
			int connectfd = events[i].data.fd;
			if (connectfd == sockfd){
				int clientfd = accept(sockfd,(struct sockaddr *)&clientaddr,&len);
				if (clientfd <= 0) continue;;

				printf(" clientfd:%d\n",clientfd);
				ev.events=EPOLLIN | EPOLLET;
				ev.data.fd=clientfd;
				epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd ,&ev );
			}
			else if (events[i].events & EPOLLIN ){
                while (1){
                    char buffer[10]={0};
                    int ret = recv(connectfd, buffer, 10, 0);
                    if (ret == -1 && errno != EAGAIN){
                        perror("recv\n");
                        epoll_ctl(epfd, EPOLL_CTL_DEL, connectfd ,NULL );
                        close(connectfd);
                        break;
                    }
                    else if (ret > 0){
                        printf("recv: %s\n", buffer);
					    send(connectfd, buffer, ret, 0);
                    }
                    else{
                        printf("close\n");
                        epoll_ctl(epfd, EPOLL_CTL_DEL, connectfd ,NULL );
                        close(connectfd);
                    }
                }
			}
		}
	}
#endif
	getchar();
	
}

