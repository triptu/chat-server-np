
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAXLINE 300
#define BUFFER_SIZE 500
#define MAX_CLIENTS 100


int parent_mq_id, child_mq_id, useridcnt;
int running=1;   // Client running
int pidList[MAX_CLIENTS]; // of child, index = userid
int child_mqids[MAX_CLIENTS];

/* ---------------- Structures we need ------------- */

/* client */
typedef struct client_t{
	struct sockaddr_in addr; // Client address
	int connfd; 			// File descriptor
	int userId; 			// Unique assigned by server
	char name[32]; 			// Nick name
	int status; 			// Online(1) or offline(0)
	int isRegistered;
	int mqid;
}client_t;
client_t *clients[MAX_CLIENTS];
client_t *Client;

/* For message q communication with children */
typedef struct mq_msg{
	long mtype;
	struct msg_info{
		char msg[BUFFER_SIZE];
		client_t clients[MAX_CLIENTS];
		int sender; 				// sender child mq id
		int extra;  				// For sending anything else
	}info;
}mq_msg;


/* ------- Structures definition end ------ */

/* Explicit function declarations */
void send_msg2all(int uid, const char* msg, int isBroadcast);
/* ---- */

/* --------- Wrapper Functions --------------*/

void err_sys(const char* x)
{
    perror(x);
    exit(1);
}

int Fork(){
	int n;
	if((n=fork())<0)
		err_sys("Fork error");
	return n;
}

int Socket(int family, int type, int protocol){
	int n;
	if((n=socket(family, type, protocol))<0)
		err_sys("Socket error");
	return n;
}

int Bind(int sockfd, const struct sockaddr *myaddr,
		 socklen_t addrlen){
	int n;
	if((n=bind(sockfd, myaddr, addrlen))<0)
		err_sys("Bind error");
	return n;
}

int Listen(int sockfd, int backlog){
	int n;
	char *ptr;
	if((ptr = getenv("LISTENQ"))!=NULL)
		backlog = atoi(ptr);
	if((n=listen(sockfd, backlog))<0)
		err_sys("Listen error");
	return n;
}

int Accept(int sockfd, struct sockaddr *cliaddr,
		   socklen_t *addrlen){
	int n;
	if((n=accept(sockfd, cliaddr, addrlen))<0)
		err_sys("Accept error");
	return n;
}

int Close(int sockfd){
	int n;
	if((n=close(sockfd))<0)
		err_sys("Close error");
	return n;
}

int Read(int sockfd, char* line, int num){
	int n;
	if((n=recv(sockfd, line, num, 0))<0)
		err_sys("Read error");
	return n;
}

int Write(int sockfd, const char* line, int num){
	int n;
	if((n=send(sockfd, line, num, 0))<0)
		err_sys("Write error");
	return n;
}

int Msgsnd(int mqid, const mq_msg *mptr){
	int n;
	if((n=msgsnd(mqid, mptr, sizeof(struct msg_info), 0))==-1){
		err_sys("msgsnd");
	}
	return n;
}

ssize_t Msgrcv(int readid, struct mq_msg *mptr){
	ssize_t n;
	do{
		n = msgrcv(readid, mptr, sizeof(struct msg_info),0,0);
	} while(n==-1 && errno==EINTR);
	if(n==-1) err_sys("msgrcv");
	return n;
}

/* -------- Wrapper Functions end -------------*/


/* ------------ Parent Children Communication */

// Child end
// Child tells I'm quitting
void bye_parent(){
	char buff_out[BUFFER_SIZE];
	sprintf(buff_out, "[SERVER MSG]: %s has left the server.\n",
						Client->name);
	mq_msg mesg;
	mesg.mtype = 12;  // Quitting
	mesg.info.clients[0] = *Client;
	Msgsnd(parent_mq_id, &mesg);
	send_msg2all(Client->userId, buff_out, 0);
}

// Parent end
void handle_bye(client_t *client){
	char buff_out[BUFFER_SIZE];
	sprintf(buff_out, "[SERVER MSG]: %s has left the server",
						client->name);
	int i;
	pidList[client->userId] = -1;
	free(clients[client->userId]);
	clients[client->userId] = NULL;
	printf("%s, UserId = %d \n", buff_out, client->userId);
}

// get list from parent and store in update in global clien_t *clients[MAX]
void get_list(){
	mq_msg mesg;
	int i;
	mesg.mtype = 1; // Get list
	mesg.info.sender = child_mq_id;
	Msgsnd(parent_mq_id, &mesg);
	Msgrcv(child_mq_id, &mesg);
	for(i=1;i<=mesg.info.extra;i++){
		if(mesg.info.clients[i].status == -1)
			clients[i] = NULL;
		else
			clients[i] = &mesg.info.clients[i];
	}
}

void send_list(int child_mq_id){
	mq_msg mesg;
	int i;
	mesg.mtype = 1;
	mesg.info.extra = useridcnt;
	for(i=1;i<=useridcnt;i++){
		if(clients[i] == NULL)
			mesg.info.clients[i].status = -1;
		else
			mesg.info.clients[i] = *clients[i];
	}
	Msgsnd(child_mq_id, &mesg);
}


/* ------------ Parent-children Communication end ----- */


/* ---------- Child Helper Functions -------------*/

// find maximum
static inline int max(int lhs, int rhs) {
    if(lhs > rhs)
        return lhs;
    else
        return rhs;
}

// Send private message by connfd=Client->connfd
void send_pm(const char *msg, int connfd){
	Write(connfd, msg, strlen(msg));
}

/* send message to all except one user */
/* set uid=-1 for message to everyone */
void send_msg2all(int uid, const char* msg, int isBroadcast){
	int i;
	get_list();
	char buff_out[BUFFER_SIZE];
	buff_out[0] = '\0';
	if(isBroadcast)
		sprintf(buff_out, "[BROADCAST]: %s sent - ", Client->name);
	strcat(buff_out, msg);
	mq_msg mesg;
	mesg.mtype = 5;
	mesg.info.clients[0] = *Client;
	strcpy(mesg.info.msg,buff_out);
	for(i=0; i<MAX_CLIENTS;i++){
		if(clients[i] != NULL && clients[i]->userId != uid){
			Msgsnd(clients[i]->mqid, &mesg);
		}
	}
}

/* Send Private message by name */
int send_pm_name(char* name, const char* msg){
	if(!strcmp(name, "unregistered")){
		send_pm("[ERROR]: You can't message an unregistered user.\n", Client->connfd);
		return -1;
	}
	int i;
	get_list();
	mq_msg mesg;
	mesg.mtype = 2;
	strcpy(mesg.info.msg,msg);
	mesg.info.clients[0] = *Client;
	for(i=0; i<MAX_CLIENTS;i++){
		if(clients[i] != NULL && !strcmp(clients[i]->name, name)){
			Msgsnd(clients[i]->mqid, &mesg);
			return 1;
		}
	}
	if(i==MAX_CLIENTS){
		send_pm("[ERROR]: User does not exist\n", Client->connfd);
		return -1;
	}
}


/* Send list of clients */
void printList(int connfd){
	char buff_out[BUFFER_SIZE];
	int i;
	get_list();
	buff_out[0] = '\0';
	strcat(buff_out, "--------- Users List --------\n");
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			strcat(buff_out, clients[i]->name);
			if(clients[i]->status)
				strcat(buff_out, "  - Online");
			else
				strcat(buff_out, "  - Offline");
			strcat(buff_out, "\n");
		}
	}
	strcat(buff_out, "------------------------------\n");
	send_pm(buff_out, connfd);
}


/* send Help text */
void sendHelp(int userfd){
	char buff_out[BUFFER_SIZE];
	buff_out[0] = '\0';
	strcat(buff_out, "List of commands:\n");
	strcat(buff_out, "\\help              to print this help\n");
	strcat(buff_out, "\\ping              to check connection\n");
	strcat(buff_out, "\\reg <name>        to join the server\n");
	strcat(buff_out, "\\name <newname>    change name\n");
	strcat(buff_out, "\\leave             leave the server\n");
	strcat(buff_out, "\\list              list all users\n");
	strcat(buff_out, "\\all <msg>         send msg to all\n");
	strcat(buff_out, "\\pm <user> <msg>   private msg\n");
	strcat(buff_out, "\\reply <msg>       reply to person you last sent or received from\n");
	strcat(buff_out, "\\forward <name>    forward last private message sent or received\n");
	send_pm(buff_out, userfd);
}

/* Register new */
void Registered(char* new){
	char buff_out[BUFFER_SIZE];
	sprintf(buff_out, "[SYSTEM_MSG]: %s has joined.\n",
									 new);
	send_pm(buff_out, Client->connfd);
	send_msg2all(Client->userId, buff_out, 0);
}

/* Change name */
void nameChanged(char* old, char* new){
	char buff_out[BUFFER_SIZE];
	sprintf(buff_out, "[SYSTEM_MSG]: %s changed name to %s.\n",
									old, new);
	send_pm(buff_out, Client->connfd);
	send_msg2all(Client->userId, buff_out, 0);
}


/* ------------ Helper Functions end ------------------*/

/* Print ip address */
void print_addr(struct sockaddr_in addr){
	printf("%d.%d.%d.%d",
		addr.sin_addr.s_addr & 0xFF,
		(addr.sin_addr.s_addr & 0xFF00)>>8,
		(addr.sin_addr.s_addr & 0xFF0000)>>16,
		(addr.sin_addr.s_addr & 0xFF000000)>>24);
}
// strips new line or carriage return
void strip(char* s){
	while(*s != '\0'){
		if(*s=='\r' || *s=='\n'){
			*s = '\0';
			break;
		}
		s++;
	}
}

int nameExists(char* name){
	get_list();
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(!strcmp(name, clients[i]->name))
				return 1;
		}
	}
	return 0;
}

void process(int sockfd, client_t* Client){
	char line[BUFFER_SIZE];
	char *command, *nextWord, mymessage[BUFFER_SIZE], *tempword;
	char prevmessage[BUFFER_SIZE], prevName[20];
	char buff_out[BUFFER_SIZE];
	prevmessage[0]= '\0';
	prevName[0] = '\0';
	fd_set readfds;
	ssize_t n;
	int sret, mret;
	mq_msg mesg;
	struct timeval timeout;

	// (sockfd==Client->connfd);
	printf("[SYSTEM_MSG]: New connection from: ");
	print_addr(Client->addr);
	printf("  UserId = %d\n", Client->userId);
	send_pm("Hello!\n", sockfd);
	sendHelp(sockfd);

	mesg.mtype = 10;
	mesg.info.sender = child_mq_id;
	while(1){
		FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100;  // microseconds
        sret = select(sockfd+1, &readfds, NULL, NULL,&timeout);

        if(FD_ISSET(sockfd, &readfds)){
			if((n=Read(sockfd, line, MAXLINE))==0)
				break; // Client has broken the connection
			line[n] = '\0';
			strip(line);
			if(n-1 > BUFFER_SIZE){
				send_pm("[ERROR]: Message too long\n", sockfd);
				continue;
			}
			if(!strlen(line))
				continue;		// Empty line

			if(line[0]!='\\'){
				send_pm("[ERROR]: Invalid Command\n", sockfd);
				continue;
			}

			command = strtok(line, " ");

			if(!Client->isRegistered && strcmp(command, "\\reg") &&
									    strcmp(command, "\\leave") &&
									    strcmp(command, "\\help") &&
									    strcmp(command, "\\list") &&
									    strcmp(command, "\\ping")){
				send_pm("[ERROR]: Register first.\n", sockfd);
				continue;
			}


			if(!strcmp(command, "\\help")){
				sendHelp(sockfd);
			}
			else if(!strcmp(command, "\\leave")){
				Close(sockfd);
				// printf("DBG: closed client\n");
				bye_parent();
				break;
			}
			else if(!strcmp(command, "\\list")){
				printList(sockfd);
			}
			else if(!strcmp(command, "\\ping")){
				send_pm("pong\n", sockfd);
			}
			else if(!strcmp(command, "\\reg")){
				nextWord = strtok(NULL, " ");
				if(Client->isRegistered){
					send_pm("[ERROR]: You have aready registered.\n", sockfd);
				}
				else if(!nextWord){
					send_pm("[ERROR]: Name cannot be nulll\n", sockfd);
				}
				else{
					if(nameExists(nextWord)){
						send_pm("[ERROR]: Name already exists.\n", sockfd);
						continue;
					}
					mesg.mtype = 3; // Registration
					strcpy(mesg.info.msg, nextWord);
					mesg.info.sender = Client->userId;
					Msgsnd(parent_mq_id, &mesg);
					strcpy(Client->name,nextWord);
					Client->isRegistered = 1;
					Registered(nextWord);
				}
			}
			else if(!strcmp(command, "\\name")){
				nextWord = strtok(NULL, " ");
				if(!nextWord){
					send_pm("[ERROR]: Name cannot be nulll.\n", sockfd);
				}
				else{
					if(!strcmp(Client->name, nextWord)){
						send_pm("[ERROR]: It's already your name.\n", sockfd);
						continue;
					}
					if(nameExists(nextWord)){
						send_pm("[ERROR]: Name already exists.\n", sockfd);
						continue;
					}
					mesg.mtype = 4; // Name Change
					char* temp = strdup(Client->name);
					strcpy(mesg.info.msg, nextWord);
					mesg.info.sender = Client->userId;
					Msgsnd(parent_mq_id, &mesg);
					strcpy(Client->name,nextWord);
					nameChanged(temp, nextWord);
					free(temp);
				}
			}
			else if(!strcmp(command, "\\all") || !strcmp(command, "\\pm")){
				if(!strcmp(command, "\\pm")){
					nextWord = strtok(NULL, " "); // Send to this person
					if(!nextWord)
						send_pm("[ERROR]: mention who you want to send.\n", sockfd);
				}
				tempword = strtok(NULL, " ");
				if(!tempword)
					send_pm("[ERROR]: Message cannot be Empty.\n", sockfd);
				else{
					strcpy(mymessage, "");
					while(tempword!=NULL){
						strcat(mymessage, tempword);
						strcat(mymessage, " ");
						tempword = strtok(NULL, " ");
					}
					strcat(mymessage, "\n");
					if(!strcmp(command, "\\pm")){
						strcpy(prevmessage, mymessage);
						strcpy(prevName, nextWord);
						if(send_pm_name(nextWord, mymessage)!=-1){
							sprintf(buff_out, "[DELIVERY_REPORT]: %s received:- %s",
														nextWord, mymessage);
							send_pm(buff_out, sockfd);
						}
					}
					else{
						send_msg2all(Client->userId, mymessage, 1);
						printf("[BROADCASTING]: %s sent - %s",
										Client->name, mymessage);
						sprintf(buff_out, "[DELIVERY_REPORT]: You broadcasted:- %s",
														mymessage);
						send_pm(buff_out, sockfd);
					}
				}
			}
			else if(!strcmp(command, "\\forward")){
				if(prevmessage[0]=='\0')
					send_pm("[ERROR]: Noone has sent you message.\n", sockfd);
				else{
					nextWord = strtok(NULL, " "); // Send to this person
					if(!nextWord)
						send_pm("[ERROR]: mention who you want to send.\n", sockfd);
					else{
						strcpy(prevName, nextWord);
						if(send_pm_name(nextWord, prevmessage)!=-1){
							sprintf(buff_out, "[DELIVERY_REPORT]: %s received:- %s",
														nextWord, prevmessage);
							send_pm(buff_out, sockfd);
						}
					}
				}
			}
			else if(!strcmp(command, "\\reply")){
				if(prevName[0]=='\0')
					send_pm("[ERROR]: Noone has sent you message.\n", sockfd);
				else{
					tempword = strtok(NULL, " ");
					if(!tempword)
						send_pm("[ERROR]: Message cannot be Empty.\n", sockfd);
					else{
						strcpy(mymessage, "");
						while(tempword!=NULL){
							strcat(mymessage, tempword);
							strcat(mymessage, " ");
							tempword = strtok(NULL, " ");
						}
						strcat(mymessage, "\n");
						strcpy(prevmessage, mymessage);
						if(send_pm_name(prevName, mymessage)!=-1){
							sprintf(buff_out, "[DELIVERY_REPORT]: %s received:- %s",
														prevName,mymessage);
							send_pm(buff_out, sockfd);
						}
					}
				}
			}
			else{
				send_pm("[ERROR]: Invalid Command\n", sockfd);
			}
		}

		while(1){ // Empty the message queue
			mret = msgrcv(child_mq_id, &mesg,
						  sizeof(struct msg_info), 0, IPC_NOWAIT);
			// printf("DBG: msg q child\n");
			if(mret<0){
				if(errno==ENOMSG)
					break;
				else
					err_sys("msgrcv error");
			}
			else{  // Someone has sent message
				if(mesg.mtype==2){
					sprintf(buff_out, "[PRIVATE_MSG]: %s sent :- ", mesg.info.clients[0].name);
					strcat(buff_out, mesg.info.msg);
					send_pm(buff_out, sockfd);
					strcpy(prevmessage, mesg.info.msg);
					strcpy(prevName, mesg.info.clients[0].name);
				}
				else if(mesg.mtype==5){
					send_pm(mesg.info.msg, sockfd);
				}
				else{
					printf("Received a message of mtype %ld\n", mesg.mtype);
				}
			}
		}
	}

	bye_parent();
	// Delete our private queue
	if (msgctl(child_mq_id, IPC_RMID, NULL)==-1){
		perror("msgctl");
		exit(1);
	}
}


int main(int argc, char const *argv[])
{
	printf("[SYSTEM_MSG]: Starting server.\n");
	printf("[SYSTEM_MSG]: To start client open 'telnet 127.0.0.1 1234' in another terminal.\n");
	short int portno=1234;
	if(argc==2)
		portno = atoi(argv[1]);
	int listenfd, connfd;
	pid_t childpid;
	key_t serverKey;
	fd_set readfds;
	int sret, mret;
	socklen_t clilen;
	struct sockaddr_in cliaddr, servaddr;
	struct timeval timeout;
	useridcnt=0;  // for allocating new userids

	// Server Message Queue Key
	if((serverKey = ftok("chat.c", 'T')) == -1)
		err_sys("ftok");
	if((parent_mq_id=msgget(serverKey, 0666 | IPC_CREAT))==-1)
		err_sys("msgget");

	memset(&servaddr, '\0', sizeof(servaddr));
	listenfd = Socket(AF_INET, SOCK_STREAM, 0);
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(portno);
	Bind(listenfd, (struct sockaddr *) &servaddr,
		 sizeof(servaddr));
	Listen(listenfd, 5);

	mq_msg mesg;

	for(;;){
		FD_ZERO(&readfds);
        FD_SET(listenfd, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100;  // microseconds
        sret = select(listenfd+1, &readfds, NULL, NULL,&timeout);

		if(FD_ISSET(listenfd, &readfds)){  			// A new connection
			clilen = sizeof(cliaddr);
			connfd = Accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);
			if(useridcnt+1 == MAX_CLIENTS){
				printf("[SYSTEM_ERR] Can't accept more clients.\n");
				Close(connfd);
			}
			else{
				if(child_mq_id==-1){
					err_sys("msgget");
				}
				Client = (client_t *) malloc(sizeof(client_t));
				Client->userId = ++useridcnt;
				child_mq_id = msgget(useridcnt, 0666 | IPC_CREAT);
				Client->status = 1;
				Client->connfd = connfd;
				Client->isRegistered = 0;
				Client->addr = cliaddr;
				Client->mqid = child_mq_id;
				strcpy(Client->name, "unregistered");
				clients[useridcnt] = Client;

				if((childpid=Fork())==0){
					// Child process
					Close(listenfd);
					process(connfd, Client);
					exit(0);
				}
				else{
					Close(connfd);
					child_mqids[useridcnt] = child_mq_id;
					pidList[useridcnt] = childpid;
				}
			}
		}
		while(1){ // Empty the message queue
			mret = msgrcv(parent_mq_id, &mesg,
						  sizeof(struct msg_info), 0, IPC_NOWAIT);
			if(mret<0){
				if(errno==ENOMSG)
					break;
				else
					err_sys("msgrcv error");
			}
			else{  // Child has sent message
				if(mesg.mtype==1){
					// printf("DBG: Child asked for list\n");
					send_list(mesg.info.sender);
				}
				else if(mesg.mtype==12){
					// printf("DBG: Client quitting\n");
					handle_bye(&mesg.info.clients[0]);
				}
				else if(mesg.mtype==3){ //  registration
					printf("[SYSTEM_MSG]: %s has registered, userId = %d\n",
											mesg.info.msg, mesg.info.sender);
					strcpy(clients[mesg.info.sender]->name, mesg.info.msg);
					clients[mesg.info.sender]->isRegistered = 1;
				}
				else if(mesg.mtype==4){ //  Change name
					printf("[SYSTEM_MSG]: %s changed name to %s, userId = %d\n",
							clients[mesg.info.sender]->name ,mesg.info.msg,
														 mesg.info.sender);
					strcpy(clients[mesg.info.sender]->name, mesg.info.msg);
				}
				else{
					printf("Client sent msg type %ld\n", mesg.mtype);
				}
			}
		}
	}

	// Delete server queue
	if (msgctl(parent_mq_id, IPC_RMID, NULL)==-1){
		err_sys("msgctl");
	}
	return 0;
}
