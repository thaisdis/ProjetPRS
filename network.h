#ifndef sockParam_h_
#define sockParm_h_
//Cette structure permet de passer les arguments d'une socket UDP d'une fonction à une autre
typedef struct sockParam SockParam;
struct sockParam {
   int sock; //contient le descripteur de fichier de la socket
   struct sockaddr_in addr;//contient l'adresse du client qui va envoyer/recevoir des données
   char *filename;
   int *seg_num; 
};


int ths (struct sockParam sock, char *buffer, int port);
struct sockParam openDataSocket(int port);
void *sendFile (void *arg);
void *receiveACK (void *arg);
void sig_handler (int signum);

#endif
