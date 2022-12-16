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
//#define RTO 1000

#define TAILLE 15
//#define BUFFER_DATA_SIZE 1494
#define tampon 30

//Variables globales: partagées entre threads, surveillées avec mutex

int credit=WINDOW;
int last_ACK_received=0;
int theEnd=0;
int flag_fastR=0;
int flag_fastTO=0;
int TO_seg;

pthread_t t_sendFile;
pthread_t t_receiveACK;
int id_sendFile;
int id_receiveACK;
int seg_num;

int ack_count;
unsigned long tot_seg=95; // mettre le calcul en partagé

pthread_mutexattr_t attr;
pthread_mutex_t lock_flagR;
pthread_mutex_t lock_flagTO;
pthread_mutex_t lock_TO_seg;
pthread_mutex_t lock_credit;
pthread_mutex_t lock_last_ACK_received;

int main(int argc, char* argv[]) {

    int sock;
    char clientBuffer[512];
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
         if (recvfrom(sock, clientBuffer, sizeof(clientBuffer), 0, (struct sockaddr *)&arrivalSock.addr, &size_UDP)<0){
                char err[24];
                sprintf(err, "[%d]", getpid());
                strcat(err, "[ERROR]Erreur à la réception");
                perror((char *)err);
                exit(errno);
         }
                
         DataPort= ths( arrivalSock,clientBuffer,DataPort);
         printf("%s\n", clientBuffer);
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
              
               if (recvfrom(sockclient.sock, clientBuffer, sizeof(clientBuffer), 0, (struct sockaddr *)&sockclient.addr, &size_Data)<0){
                  char err[24];
                  sprintf(err, "[%d]", getpid());
                  strcat(err, "[ERROR]Erreur à la réception");
                  perror((char *)err);
                  exit(errno);
               }
               //le client demande un fichier
               printf("[%d][INFO]Client asking for file : %s\n", getpid(), clientBuffer);

               // paramètres pour lancement des threads et mesure du temps
              
               sockclient.filename=clientBuffer;
               struct timespec start, end; //pour la mesure du temps
               int filesize;
               clock_gettime(CLOCK_MONOTONIC_RAW, &start);// timer for debug

               //init des threads
               pthread_mutexattr_init(&attr);
               pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
               pthread_mutex_init(&lock_last_ACK_received, &attr);
               
               
               pthread_create(&t_sendFile, NULL,sendFile, &sockclient);
               pthread_create(&t_receiveACK, NULL,receiveACK, &sockclient);
              
               pthread_join(t_sendFile, (void*)&filesize);
               pthread_join(t_receiveACK, NULL);
              
               pthread_mutex_destroy(&lock_credit );
               pthread_mutex_destroy(&lock_flagR);
               pthread_mutex_destroy(&lock_flagTO);
               pthread_mutex_destroy(&lock_last_ACK_received );

               memset(clientBuffer, 0, sizeof(&clientBuffer));
               close(sockclient.sock);

               clock_gettime(CLOCK_MONOTONIC_RAW, &end);
               double delta_us = (double)(end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;
               int debit = (double) filesize/delta_us;
               
               printf("[%d][INFO]File send with %d\n", getpid(), debit);
               printf("This is the end\n");
            }

            exit(process);
            return(EXIT_SUCCESS);//on ferme le processus fils
         }
        }
   }
   close(sock);
   return 0;
}
