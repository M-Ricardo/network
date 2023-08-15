//创建MAX_PORT个监听套接字

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
#define EVENTS_LEN 1024
#define MAX_PORT   100  
typedef int (*ZVCALLBACK)(int fd, int events, void *arg);

typedef struct zv_connect_s
{
	int fd;
	ZVCALLBACK cb;  //回调函数

	char rbuffer[BUFFER_LEN];  //存储读取的数据
	int rc; 	//rbuffer的长度
	int count; 	//决定每次读多少字节
	char wbuffer[BUFFER_LEN]; //存储待发送的数据
	int wc; 	//wbuffer的长度
} zv_connect_t;

typedef struct zv_connblock_s{
	zv_connect_t *block;
	struct zv_connblock_s *next;
} zv_connblock_t;

typedef struct zv_reactor_s{
	int epfd;
	int blkcont;

	zv_connblock_t *blockheader;
} zv_reactor_t;


//开辟reactor的内存空间
int zv_init_reactor(zv_reactor_t* reactor){
	if (!reactor) return -1;
#if 0
	// 分配两块不连续的空间
	reactor->blockheader=malloc(sizeof(zv_connblock_t));
	if (reactor->blockheader == NULL) return -1;

	reactor->blockheader->block=calloc(1024,sizeof(zv_connect_t));
	if (reactor->blockheader->block == NULL) return -1;
#elif 1
	//分配两块连续的空间
	reactor->blockheader=(zv_connblock_t *)malloc(sizeof(zv_connblock_t)+EVENTS_LEN*sizeof(zv_connect_t));
	if (reactor->blockheader == NULL) return -1;

	reactor->blockheader->block = (zv_connect_t *)(reactor->blockheader+1);
#endif
	reactor->blkcont = 1;
	reactor->blockheader->next =NULL;
	
	reactor->epfd = epoll_create(1);
}

//释放
void zv_destory_reactor(zv_reactor_t* reactor){
	if (!reactor) return ;

	if (!reactor->blockheader) free(reactor->blockheader);

	close(reactor->epfd);
}

//若空间不够，再次开辟
int zv_connect_block(zv_reactor_t *reactor){

	if (!reactor) return -1;

	zv_connblock_t *blk = reactor->blockheader;

	while (blk->next != NULL) blk = blk->next;

	zv_connblock_t *connblock=(zv_connblock_t *)malloc(sizeof(zv_connblock_t)+EVENTS_LEN*sizeof(zv_connect_t));
	if (connblock == NULL) return -1;

	connblock->block = (zv_connect_t *)(connblock+1);
	connblock->next =NULL;

	blk->next = connblock;
	reactor->blkcont ++; 

	return 0;

}

//返回第几个zv_connblock_t中的第几个zv_connect_t
zv_connect_t *zv_connect_idx(zv_reactor_t *reactor, int fd){
	if (!reactor) return NULL;

	int blockidx = fd/EVENTS_LEN;

	while (blockidx >= reactor->blkcont){
		//再开辟空间
		zv_connect_block(reactor);
	}

	int i = 0;
	zv_connblock_t *blk = reactor->blockheader;
	while(i++ < blockidx){
		blk = blk->next;
	}

	return &blk->block[fd % EVENTS_LEN];
}

//设置服务器
int init_server(short port){
	int sockfd=socket(AF_INET,SOCK_STREAM,0);

	struct sockaddr_in servaddr;
	memset(&servaddr,0,sizeof(struct sockaddr_in));
	servaddr.sin_family=AF_INET;
	servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
	servaddr.sin_port=htons(port);

	if (-1 == bind(sockfd,(struct sockaddr *)&servaddr,sizeof(struct sockaddr))){
		printf("bind failed: %s",strerror(errno));
		return -1;
	}

	listen(sockfd,10);

	printf("listen port: %d\n",port);
	
	return sockfd;
}

int recv_cb(int fd, int event, void *arg);

//发送数据
int send_cb(int fd, int event, void *arg){

	zv_reactor_t *reactor = (zv_reactor_t *)arg;
	zv_connect_t *conn = zv_connect_idx(reactor, fd);

    send(fd, conn->wbuffer, conn->wc, 0);

    conn->cb = recv_cb;


	struct epoll_event ev;
	ev.events=EPOLLIN;
	ev.data.fd=fd;
	epoll_ctl(reactor->epfd, EPOLL_CTL_MOD, fd ,&ev );
}
//接收数据
int recv_cb(int fd, int event, void *arg){

	zv_reactor_t *reactor = (zv_reactor_t *)arg;
	zv_connect_t *conn = zv_connect_idx(reactor, fd);

	//conn->rbuffer+conn->rc 是一个指针运算,从当前 rbuffer 已经存储的位置开始继续读取数据
	int ret = recv(fd,conn->rbuffer+conn->rc,conn->count,0);
	if (ret < 0){

	}
	else if (ret == 0){
		//释放空间，以供下个使用
		conn ->fd = -1;
		conn ->rc = 0;
		conn ->wc = 0;
		//移除
		epoll_ctl(reactor->epfd,EPOLL_CTL_DEL,fd,NULL);
		//关闭
		close(fd);

		return -1;
	}
#if 1
	conn->rc += ret;
	printf("rbuffer:  %s, rc: %d\n", conn->rbuffer, conn->rc);

	//为了将读写分开，把要发送到数据，存到wbuffer
    memcpy(conn->wbuffer, conn->rbuffer, conn->rc);
    conn->wc = conn->rc;
    

	//置为写事件
    conn->cb = send_cb;


	struct epoll_event ev;
	ev.events=EPOLLOUT;
	ev.data.fd=fd;
	epoll_ctl(reactor->epfd, EPOLL_CTL_MOD, fd ,&ev );

#else
	conn->rc += ret;
	printf("rbuffer:  %s, rc: %d\n", conn->rbuffer, conn->rc);
	conn->count = 20;

	struct epoll_event ev;
	ev.events=EPOLLIN;
	ev.data.fd=fd;
	epoll_ctl(reactor->epfd, EPOLL_CTL_ADD, fd ,&ev );
#endif
}

//建立连接
int accept_cb(int fd, int events, void *arg){

	struct sockaddr_in clientaddr;
	socklen_t len = sizeof(struct sockaddr);
	
	int clientfd = accept(fd,(struct sockaddr *)&clientaddr,&len);
	if (clientfd < 0) {
		printf("accept errno: %d\n", errno);
		return -1;
	}

	printf(" clientfd:%d\n",clientfd);

	/*建立连接请求之后，把原来的listen fd 置为 clientfd ，回调事件由原来的accept_cb置为recv_cb
	  再调用epoll_ctl*/
	zv_reactor_t *reactor = (zv_reactor_t *)arg;
	zv_connect_t *conn = zv_connect_idx(reactor, clientfd);

	conn->fd = clientfd;
	conn->cb = recv_cb;
	conn->count = BUFFER_LEN;

	struct epoll_event ev;
	ev.events=EPOLLIN;
	ev.data.fd=clientfd;
	epoll_ctl(reactor->epfd, EPOLL_CTL_ADD, clientfd ,&ev );
}

//创建epoll实例
int set_listen(zv_reactor_t *reactor, int fd, ZVCALLBACK cb){

	if (!reactor || !reactor->blockheader ) return -1;

	reactor->blockheader->block[fd].fd = fd;
	reactor->blockheader->block[fd].cb = cb;

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fd;

	epoll_ctl(reactor->epfd,EPOLL_CTL_ADD,fd,&ev);
}

int main(int argc, char *argv[]){

	if (argc < 2) return -1;
	int post = atoi(argv[1]);
	

	//printf("sockfd: %d\n",sockfd);

	zv_reactor_t reactor;

	zv_init_reactor(&reactor);

	//创建MAX_PORT个监听套接字
	int i=0;
	for (i=0;i<MAX_PORT;i++){
		int sockfd = init_server(post+i);
		set_listen(&reactor, sockfd, accept_cb);
	}

	struct epoll_event events[EVENTS_LEN] = {0};
	while(1){
		int nready = epoll_wait(reactor.epfd, events, EVENTS_LEN, -1);
		if (nready < 0) continue;

		int i = 0;
		for (i = 0;i<nready;i++){
			int connfd = events[i].data.fd;
			zv_connect_t *conn = zv_connect_idx(&reactor, connfd);

			if (events[i].events & EPOLLIN){ //读事件（连接请求、接收消息）
				conn->cb(connfd, events[i].events, &reactor);
			}
			if (events[i].events & EPOLLOUT){ //写事件（发送消息）
				conn->cb(connfd, events[i].events, &reactor);
			}
		}
	}

	getchar();
	
}

