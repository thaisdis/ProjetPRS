#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "network.h"
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>


#define WINDOW 6
#define RTO 10000

#define TAILLE 15
#define BUFFER_DATA_SIZE 1494
#define tampon 30


pthread_t t_sendFile;
pthread_t t_receiveACK;

//Variables globales: partagées entre threads, surveillées avec mutex

int credit= tampon;
int last_ACK_received=0;
int theEnd=0;
int flag_fastR=0;

pthread_t t_sendFile;
int id_sendFile;
pthread_t t_receiveACK;
int id_receiveACK;
int seg_num;

int ack_count;
int tot_seg=95; // mettre le calcul en partagé

pthread_mutex_t lock_flag;
pthread_mutex_t lock_credit;
pthread_mutex_t lock_last_ACK_received;

typedef struct sockParam SockParam;


int ths (struct sockParam sock, char *buffer, int port){
  
   socklen_t size_UDP = sizeof(sock.addr);  
  

   //on vérifie qu'il s'agit du bon message
   if (strcmp(buffer, "SYN") == 0){
      printf("[%d][INFO]New client UDP incoming, sending SYN\n", getpid());
      port++;
      char str[15], portToSend[15];
      strcpy(str, "SYN-ACK");
      sprintf(portToSend, "%d", port);
      strcat(str, portToSend);
      printf("[%d][INFO]Sending port number : %s\n", getpid(), str);
      sendto(sock.sock, (const char *)str, sizeof(str), MSG_CONFIRM, (struct sockaddr *)&sock.addr, size_UDP);
      memset(buffer, 0, sizeof(&buffer));
      
      if (recvfrom(sock.sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sock.addr, &size_UDP)<0){
         char err[24];
         sprintf(err, "[%d]", getpid());
         strcat(err, "[ERROR]Erreur à la réception");
         perror((char *)err);
         exit(errno);
      }
      
      if (strcmp(buffer, "ACK") == 0){
        printf("[%d][ACK]TCP connection established\n", getpid());
      }
   }
   else{
     printf("[%d][ERROR]Wrong mess from client, closing\n", getpid());
   }
   memset(buffer, 0, sizeof(&buffer));
   return port;
}

//Fonction gérant l'ouverture de la socket pour l'envoi des messages utiles ie les fichiers
struct sockParam openDataSocket(int port){
  
   struct sockaddr_in client_addr;
   memset((char*)&client_addr, 0, sizeof(client_addr));
   struct sockParam DataSock;
   DataSock.sock=socket(AF_INET, SOCK_DGRAM, 0);
   client_addr.sin_family = AF_INET;
   client_addr.sin_port = htons(port);
   client_addr.sin_addr.s_addr=INADDR_ANY;
  
   if (DataSock.sock<0){
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]Sock creation error");
      perror((char *)err);
      exit(errno);
   }
   struct sockaddr* addr_UDP = (struct sockaddr*)&client_addr;
  
   int reuse =1;  
   setsockopt(DataSock.sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

   if (bind(DataSock.sock, addr_UDP, sizeof(struct sockaddr_in))<0){
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]Erreur de bind UDP");
      perror((char *)err);
      exit(errno);
   }
   listen (DataSock.sock, 10);
   printf("[%d][INFO]Sock created\n", getpid());
   return DataSock;  
}

void sig_handler (int signum){
   //printf("en didi du signal\n");
   pthread_mutex_lock(&lock_flag);
   flag_fastR= 1;
   pthread_mutex_unlock(&lock_flag);
}

//Envoie d'un fichier à un client, passage de la structure SockParam en argument
void *sendFile (void *arg){
  

   unsigned long file_size;
   struct stat sb;
   int n;
   int seg_num = 1;
   int lost=0;
   int chunk_file;
   int taille;
   int last=0;

   struct sockaddr_in data_receive;
   int sending = 1;

   //Récupération des paramètres
   struct sockParam sock= ((struct sockParam *)arg)[0];
   int server_sock= sock.sock;
   struct  sockaddr_in addr  = sock.addr;
   socklen_t sock_size = sizeof(addr);
   char *filename= sock.filename;
  
   //Ouverture du fichier
   FILE *file = fopen(filename, "rb");
   printf("[%d][INFO]File to send : %s\n", getpid(),filename);
   if (file == NULL) {
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]File couldn't be open\n");
      perror((char *)err);
      exit(errno);
   }

   //Récupération de la taille du fichier
   if (stat(filename, &sb) == -1) {
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]File couldn't be open\n");
      perror((char *)err);
      exit(EXIT_FAILURE);
   }
   file_size = sb.st_size;
   printf("[%d][INFO]The size of the file is %lu bytes\n", getpid(), file_size);

   //Calcul du nb de segments à envoyer

   if(file_size%BUFFER_DATA_SIZE == 0){
      tot_seg = file_size / BUFFER_DATA_SIZE;
   }else{
      tot_seg = file_size / BUFFER_DATA_SIZE + 1; // si ce n'est pas un multiple de BUFFER_DATA_SIZE
   }
    printf("[%d][INFO]The number of segments to send is %d \n", getpid(), tot_seg);

    //Creation d'un buffer pour stocker tous le contenu du fichier et y accéder plus rapidement
   char segment[BUFFER_DATA_SIZE];
	char *buffer[tot_seg];
   int size_last_seg = (file_size%BUFFER_DATA_SIZE);
      
   //En cas de questions, surtout ne pas insister, y a que comme ça que ça marche lol
   for (int i = 0; i<tot_seg; i++){
      buffer[i] = malloc(sizeof(char)*BUFFER_DATA_SIZE);
   }
   
   for (int i = 0; i<tot_seg-1; i++){	
		fread(segment, BUFFER_DATA_SIZE, 1, file);
		strcpy(buffer[i], segment);
	}
	
   //Gestion à part du dernier segment
	char last_seg[size_last_seg];
	fread(last_seg, size_last_seg, 1, file);
   
   fclose(file);  
  
   // changer pour ast ack well received, sinon prend pas le dernier packet
   while(tot_seg!=last_ACK_received){ // Si je n'ai pas grillé ma fenêtre et que j'ai encore de quoi envoyer, lets go
      
      char seg[BUFFER_DATA_SIZE+6];
      memset(seg, 0, sizeof(seg));

      if (flag_fastR==1){ //on perd l'interet du signal, A CHANGER!!!!
         pthread_mutex_lock(&lock_last_ACK_received);
         lost=last_ACK_received+1;
         pthread_mutex_unlock(&lock_last_ACK_received);
         printf("c cho on a perdu un paketoooo les reufs, PAQUETO PERDU:%d \n", lost);
         
         sprintf(seg,"%06d",lost);
         taille = sizeof(char)*BUFFER_DATA_SIZE;
         memcpy(seg+6,buffer[lost-1], taille);
         printf("[%d][INFO]Sending lost seg %s\n", getpid(), seg);  
          
         sendto(server_sock, seg, sizeof(seg), MSG_CONFIRM, (struct sockaddr *)&addr, sock_size);

         pthread_mutex_lock(&lock_flag);
         flag_fastR= 0;
         pthread_mutex_unlock(&lock_flag);
        
      }

      while(credit&&sending>0){  
            if(seg_num == tot_seg){ // Si c'est le dernier segment, on arrête d'envoyer des nouveaux segments + traitement spécial du dernier segment 
               char last[size_last_seg+6];
               memset(last, 0, sizeof(last));
               sprintf(last,"%06d",seg_num);
               taille = sizeof(char)*size_last_seg;
               memcpy(last+6,last_seg, taille);
               printf("[%d][INFO]Sending last seg, size = %s\n", getpid(), last);
               sendto(server_sock, (const char *)last, sizeof(last), MSG_CONFIRM, (struct sockaddr *)&addr, sock_size);
               sending=0;
            }

         //nouvelle version
         
         sprintf(seg,"%06d",seg_num);
         taille = sizeof(char)*BUFFER_DATA_SIZE;
         memcpy(seg+6,buffer[seg_num-1], taille);
        
         printf("[%d][INFO]Sending seg %d\n", getpid(), seg_num);
         sendto(server_sock, (const char *)seg, sizeof(seg), MSG_CONFIRM, (struct sockaddr *)&addr, sock_size);
         seg_num+=1;
         pthread_mutex_lock(&lock_credit);
         credit --;
         pthread_mutex_unlock(&lock_credit);
         //printf("credit send :%d\n", credit);      
            
      }
      
   }
   char *fin = "FIN";//on annonce au client la fin de la transmission
   sendto(sock.sock, (const char *)fin, sizeof(fin), MSG_CONFIRM, (struct sockaddr *)&sock.addr, sock_size);
   //clock_gettime(CLOCK_MONOTONIC_RAW, &end);
   printf("[%d][INFO] Transmission terminée\n", getpid());
   
   free(buffer);
   
   return NULL;

}


void *receiveACK(void *arg){
  
   //Récupération des paramètres
   struct sockParam sock= ((struct sockParam *)arg)[0];
   int server_sock= sock.sock;
   struct  sockaddr_in addr  = sock.addr;
   socklen_t sock_size = sizeof(addr);
  
  
   char buffer_rcv[BUFFER_DATA_SIZE];
   int n;
   int ACK_duplicate=0;  
   int ACK_resent[BUFFER_DATA_SIZE]={0};

   signal(SIGUSR1, sig_handler);

   while(last_ACK_received!=tot_seg){
      //sleep(15);
      //nanosleep((const struct timespec[]){0,100000000L}, NULL); //utile pour bien observer les paquets
      //printf("i am here");
      recvfrom(server_sock, buffer_rcv, sizeof(buffer_rcv),0, (struct sockaddr *)&addr, &sock_size);
      //printf("Received ACK nb: %s\n", buffer_rcv);
      ack_count++;
      //printf("ack count:%d\n", ack_count);
      //printf("ack duplicate:%d\n", ACK_duplicate);
      
      int last_ACK= last_ACK_received;

      int current_ACK= atoi(&buffer_rcv[3]);
    
      if(last_ACK==current_ACK){
         //printf("fucked\n");
         pthread_mutex_lock(&lock_credit);
         credit= credit;
         pthread_mutex_unlock(&lock_credit);
         ACK_duplicate++;
         ack_count--;
         //printf("DUPLICATE:%d\n", ACK_duplicate);
         //if(ACK_duplicate>1 && ACK_resent[ack_count]==0){// principe du fastR est de spam le résau de
         if(ACK_duplicate==2){        
            pthread_kill(t_sendFile,SIGUSR1);
            //ACK_resent[ack_count]==1;
            ACK_duplicate=0;  

            
         }
      }

      
      //Passage en info à l'autre thread
      //printf("on arrive ici ou pas:%d\n", current_ACK);
      //pthread_mutex_lock(&lock);
      /*printf("global last B:%d\n", last_ACK );
      printf("current B:%d\n", current_ACK);
      printf("local last B:%d\n", last_ACK_received );*/

      //pour être sur d'avoir le dernier même si envoi dans le désordre
      if(current_ACK<last_ACK){
         current_ACK=last_ACK;
         /* pthread_mutex_lock(&lock_credit);
         credit= credit+1;
         pthread_mutex_unlock(&lock_credit); */
          
      

      } else {
         pthread_mutex_lock(&lock_credit);
         credit= credit+(current_ACK-last_ACK_received) ;
         pthread_mutex_unlock(&lock_credit);
      }
      
      // whatever happens last thing we know:
      pthread_mutex_lock(&lock_last_ACK_received);
      last_ACK_received=current_ACK;
      pthread_mutex_unlock(&lock_last_ACK_received);
      /*printf("global last A:%d\n", last_ACK );
      printf("current A:%d\n", current_ACK);
      printf("local last A:%d\n", last_ACK_received );*/


      //printf("credit receive:%d\n", credit);
    


   }
   return NULL;  
}


int main(int argc, char* argv[]) {

    int sock;
    char buffer[512];
    int port = atoi(argv[1]);
    int DataPort = port+10;
    fd_set sockets;
    FD_ZERO(&sockets);
    struct timeval* timeout=NULL;

    //UDP socket pour tous les clients
   struct sockaddr_in UDP_addr;
   memset((char*)&UDP_addr, 0, sizeof(UDP_addr));
   sock=socket(AF_INET, SOCK_DGRAM, 0);
   UDP_addr.sin_family = AF_INET;
   UDP_addr.sin_port = htons(port);
   UDP_addr.sin_addr.s_addr=INADDR_ANY;

   if (sock<0){
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]Sock creation error");
      perror((char *)err);
      exit(errno);
   }
   printf("[%d][INFO]Adresse du serveur (format IPv4) : %s\n",  getpid(), inet_ntoa(UDP_addr.sin_addr));
   printf("[%d][INFO]Port UDP pour l'arrivée de nouveau client : %d\n",  getpid(), UDP_addr.sin_port);
        
   // Bind the socket with the server address
  
   int reuse =1;  
   setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

   if (bind(sock, (struct sockaddr*)&UDP_addr, sizeof(struct sockaddr_in))<0){
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]Erreur de bind UDP");
      perror((char *)err);
      exit(errno);
   }
  
   listen(sock, 10);
  
   struct sockParam arrivalSock;//arrivalSock.addr contient une adresse vide qui sera déclarée à la réception d'un message reçu sur la socket
   arrivalSock.sock = sock;
   socklen_t size_UDP = sizeof(arrivalSock.addr);
   while(1){
        FD_SET(sock, &sockets);
        int n = select(FD_SETSIZE, &sockets, NULL, NULL, timeout);//select est en attente d'une connexion, renvoie un integer, permet de libérer du CPU tans qu'il n'y a pas de client      
        printf("[%d][INFO]En attente de connexion...\n", getpid());
        if (FD_ISSET(sock, &sockets)){
         //arrivée d'un nouveau client
         if (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&arrivalSock.addr, &size_UDP)<0){
                char err[24];
                sprintf(err, "[%d]", getpid());
                strcat(err, "[ERROR]Erreur à la réception");
                perror((char *)err);
                exit(errno);
                }
                
            DataPort= ths( arrivalSock,buffer,DataPort);
            printf("%s\n", buffer);
            struct sockParam sockclient = openDataSocket(DataPort);//ouverture de la socket pour les messages utiles, il faut le mettre avant le fork pour éviter que le client n'envoie avant la création de la socket
         int process = fork();//on fork après le ths pour être sûr que le process père incrémente la valeur de dataport pour le prochain client
        
         //le process père affiche le PID du fils et se met en attente de nouvelles connexions
         if (process !=0){
            printf("[%d][INFO]PID du client :%d\n", getpid(), process);
            close(sockclient.sock);
            //return (EXIT_SUCCESS);
         }
         //on gère la connexion dans le processus fils
         else{
            FD_SET(sockclient.sock, &sockets);//on rajoute la socket à la liste des descripteurs de fichier
            int n = select(FD_SETSIZE, &sockets, NULL, NULL, timeout);//select est en attente d'une connexion, renvoie un integer, permet de libérer du CPU tans qu'il n'y a pas de client
            
            if (FD_ISSET(sockclient.sock, &sockets)){
               //On attends que le client confirme l'ouverture de la nouvelle socket
               socklen_t size_Data = sizeof(sockclient.addr);
              
               if (recvfrom(sockclient.sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&sockclient.addr, &size_Data)<0){
                  char err[24];
                  sprintf(err, "[%d]", getpid());
                  strcat(err, "[ERROR]Erreur à la réception");
                  perror((char *)err);
                  exit(errno);
               }
               //le client demande un fichier
               printf("[%d][INFO]Client asking for file : %s\n", getpid(), buffer);

               // paramètres pour lancement des threads
              
               sockclient.filename=buffer;
              
               // // mieux comprendre ce que ça fait la
              
               pthread_create(&t_sendFile, NULL,sendFile, &sockclient);
               pthread_create(&t_receiveACK, NULL,receiveACK, &sockclient);
              
               pthread_join(t_sendFile, NULL);
               pthread_join(t_receiveACK, NULL);
              
               pthread_mutex_destroy(&lock_credit );
               pthread_mutex_destroy(&lock_flag );
               pthread_mutex_destroy(&lock_last_ACK_received );

               memset(buffer, 0, sizeof(&buffer));
               close(sockclient.sock);
            }
            
            exit(process);
            return(EXIT_SUCCESS);//on ferme le processus fils
         }
        }
   }
   close(sock);
   return 0;
}
