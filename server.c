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
#include <signal.h> //meilleure idée? je sais pas, signal pas le plus robuste, ok for m


#define WINDOW 6
#define RTO 10000

#define TAILLE 15
#define BUFFER_DATA_SIZE 1494
#define BUFFER_ACK_SIZE 2
#define tampon 30


pthread_t t_sendFile;
pthread_t t_receiveACK;

//Variables globales (partagées entre threads)

int credit= tampon;
int last_ACK_received=0;
int theEnd=0;
int flag_fastR=0;

pthread_mutex_t lock;

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


//Envoie d'un fichier à un client donné, l'adresse et la socket correspondante sont passées à l'aide de la struct sockParam
// rfaire passage en param + propore
void *sendFile (void *arg){

   unsigned long file_size;
   struct stat sb;
   char send_buffer[BUFFER_DATA_SIZE];
   int taille = BUFFER_DATA_SIZE;
   int n;

  

   //get arguments
   struct sockParam sock= ((struct sockParam *)arg)[0];

   int server_sock= sock.sock;
   struct  sockaddr_in addr  = sock.addr;
   socklen_t sock_size = sizeof(addr);
   char *filename= sock.filename;
  
   /*
   struct thread_data *info = data;
   struct sockaddr_in addr= inf addr;
   int sock_in = inf sock_in;
   char *filename= inf filename;*/


   //ouverture du fichier
   FILE *file = fopen(filename, "r+");
   printf("[%d][INFO]File to send : %s\n", getpid(),filename);
   if (file == NULL) {
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]File couldn't be open\n");
      perror((char *)err);
      exit(errno);
   }

   //on récupère la taille du fichier
   if (stat(filename, &sb) == -1) {
      char err[24];
      sprintf(err, "[%d]", getpid());
      strcat(err, "[ERROR]File couldn't be open\n");
      perror((char *)err);
      exit(EXIT_FAILURE);
   }

   file_size = sb.st_size;
   printf("[%d][INFO]The size of the file is %lu bytes\n", getpid(), file_size);

   //on calcule le nb de segments à envoyer
   int tot_seg;
   if(file_size%BUFFER_DATA_SIZE==0){
      tot_seg = file_size / BUFFER_DATA_SIZE;
   }else{
      tot_seg = file_size / BUFFER_DATA_SIZE + 1; // si ce n'est pas un multiple de BUFFER_DATA_SIZE
   }
   printf("[%d][INFO]The number of segments to send is %lu \n", getpid(), tot_seg);

   int ACK[tot_seg];//futur mutex, mais déjà à la bonne taille XD


   char datas[tot_seg][BUFFER_DATA_SIZE];
      **datas=(char*)malloc((sizeof(*datas)*tot_seg));
   for (int i = 0; i<tot_seg; i++){
      strcpy(datas[i],(char*)malloc(sizeof(**datas)));//Or on ne peut pas affecter une string à un tableau. On ne peut que "remplir" un tableau.
      fgets(datas[i], BUFFER_DATA_SIZE, file); // rajoute des 00 en fin de chaine, mais balec pr l'instant
      ACK[i] = 0;
   }
  

   int seg_num = 1;
   int lost=0;
   int last_sent=0;

   struct sockaddr_in data_receive;
   int sending = 1;


  
  // while(lost!= tot_seg ){ //pas bon pcq ack arrivent dans le desordre
   //if(flag_fastR==0){ //oui mais ducoup c pas instant


   if(sending&&credit>0){ // echanger avec  au dessus, juste important d'avoir le bon last sent en mémoir et pas un trop loin

      for (int i=0; i<tot_seg; i++){
          /* pthread_mutex_lock(&lock);
            lost=last_ACK_received;
            pthread_mutex_unlock(&lock);*/
         if(seg_num == tot_seg){ // Si c'est le dernier segment, on arrête d'envoyer des nouveaux segments
            taille =(file_size%BUFFER_DATA_SIZE);
            printf("[%d][INFO]Sending last seg, size = %lu \n", getpid(), taille);    
            
            
            sending =0;
         }

         char seg[BUFFER_DATA_SIZE+6];//BUFFER_DATA_SIZE bytes lu + 6 bytes de la seguence
         memset(seg,0, sizeof(seg));//T6hais regarde les memset
         sprintf(seg, "%06d",seg_num);//06 fait automatiquement la mise en forme
         printf("[%d][INFO]Sending seg %s\n", getpid(), seg);
        
        
         //Lecture du buffer à envoyer dans le fichier
         memcpy(seg+6, datas[i], taille);//-1 car le client n'écrit qu'à partir de la séquence 1
        
         nanosleep((const struct timespec[]){0,100000000L}, NULL);//pour comprendre ce qui se passe
         //On envoit le segment
         sendto(server_sock, (const char *)seg, sizeof(seg), MSG_CONFIRM, (struct sockaddr *)&addr, sock_size);
            //printf("[%d][ERROR]Error in the sending of seg %s\n", getpid(), seg_num);
        
         //Le seg n a été envoyé en dernier, je stocke sa valeur, utile si le packet a été perdu
         last_sent=seg_num;

         if(flag_fastR==1){ //on perd l'interet du signal, A CHANGER
            pthread_mutex_lock(&lock);
            lost=last_ACK_received+1;
            pthread_mutex_unlock(&lock);
            printf("c cho on a perdu un paketoooo les reufs, PAQUETO PERDU:%d \n", lost);

            // RENVOI
            sprintf(seg, "%06d",lost);//06 fait automatiquement la mise en forme
            printf("[%d][INFO]Sending lost seg %s\n", getpid(), seg);    
            memcpy(seg+6, datas[i], taille);
            nanosleep((const struct timespec[]){0,100000000L}, NULL);
            sendto(server_sock, (const char *)seg, sizeof(seg), MSG_CONFIRM, (struct sockaddr *)&addr, sock_size);
            pthread_mutex_lock(&lock);
            flag_fastR= 0;
            credit --; // à re réflechir
            pthread_mutex_unlock(&lock);
              

         }
        
         seg_num+=1;
      
        
         // Gestion fenêtre, un segment envoyé, je dois recevoir un ack
         pthread_mutex_lock(&lock);
         credit --;
         pthread_mutex_unlock(&lock);
      }
   }

  

   //}else{
      //printf("c cho on a perdu un paketoooo les reufs\n");
     // theEnd = 1;
     // fclose(file);
      //return NULL;
   //}
   //nanosleep((const struct timespec[]){0,100000000L}, NULL);//force sleep fpr 100ms problemo à regler, le bail join trop tot pas le temps pr les ack
   theEnd = 1;
   fclose(file);
   return NULL;
    

}


void sig_handler (int signum){
   printf("I AM HANDLING\n");
   pthread_mutex_lock(&lock);
   flag_fastR= 1;
   // credit doit bouger??
   pthread_mutex_unlock(&lock);
  

}


void *receiveACK(void *arg){

   //get arguments
   struct sockParam sock= ((struct sockParam *)arg)[0];
   int server_sock= sock.sock;
   struct  sockaddr_in addr  = sock.addr;
   socklen_t sock_size = sizeof(addr);
  

   printf("[%d][INFO]Entering thread for receiving ACKs...\n", getpid());

   char buffer_rcv[BUFFER_DATA_SIZE];

   //int buffer_ACK[BUFFER_ACK_SIZE]={0};
   int ACK_duplicate=0;
  
   int n;
   int last_ACK=0, current_ACK=0;  
   // check si il est sur le bon ack nb, envoi sig sinon
  

  
   while(1){
      nanosleep((const struct timespec[]){0,100000000L}, NULL); //utile pour bien observer les paquets

      n= recvfrom(server_sock, buffer_rcv, sizeof(buffer_rcv), MSG_DONTWAIT, (struct sockaddr *)&addr, &sock_size);
      // rajouter erreur mess

     // if(n>0){

      //printf("actu:%s\n", buffer_rcv );  
      // stockage du dernier ACK reçu
      last_ACK= current_ACK;
      //printf("Last:%d\n", last_ACK);  
      // mise à jour du nouvel ACK
      current_ACK= atoi(&buffer_rcv[3]);

      //printf("Current:%d\n", current_ACK);  
      
      if(last_ACK==current_ACK){
         ACK_duplicate++;
         if(ACK_duplicate==3){
            pthread_mutex_lock(&lock);
            last_ACK_received= current_ACK;
            //printf("c cho on a perdu un paketoooo les reufs, PAQUETO PERDU:%d \n", current_ACK+1);
            pthread_mutex_unlock(&lock);
            kill(getpid(),SIGUSR1);
            ACK_duplicate=0;
         }
      }
      //pour être sur d'avoir le dernier même si envoi dans le désordre //pk c grave.?? pas besoin pcq on check pas si c les memes ??
         if(current_ACK<last_ACK){
         current_ACK=last_ACK;
      }  
      
      
      //Passage en info à l'autre thread
      pthread_mutex_lock(&lock);
      last_ACK_received= current_ACK;
      pthread_mutex_unlock(&lock);
  
      //Gestion fenêtre: Un ack reçu, donc un credit en plus
      pthread_mutex_lock(&lock);
      credit= credit+(current_ACK-last_ACK) ;
      pthread_mutex_unlock(&lock);
  
      
      if(theEnd==1){
       break;
      }
        
      
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
              
               sockclient.filename= buffer;
              
               signal(SIGUSR1, sig_handler); // mieux comprendre ce que ça fait la
               pthread_create(&t_sendFile, NULL,sendFile, &sockclient);
               pthread_create(&t_receiveACK, NULL,receiveACK, &sockclient);
              
               pthread_join(t_sendFile, NULL);
               pthread_join(t_receiveACK, NULL);
              


               pthread_mutex_destroy(&lock);

               char *fin = "FIN";//on annonce au client la fin de la transmission
               sendto(sockclient.sock, (const char *)fin, sizeof(fin), MSG_CONFIRM, (struct sockaddr *)&sockclient.addr,sizeof(sockclient.addr) );
               printf("[%d][INFO]Envoi terminé\n", getpid());
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