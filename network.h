#ifndef sockParam_h_
#define sockParm_h_
//Cette structure permet de passer les arguments d'une socket UDP d'une fonction à une autre
typedef struct sockParam SockParam;
struct sockParam {
   int sock; //contient le descripteur de fichier de la socket
   struct sockaddr_in addr;//contient l'adresse du client qui va envoyer/recevoir des données
   char *filename; 
};

int ths (struct sockParam sock, char *buffer, int port);
struct sockParam openDataSocket(int port);
<<<<<<< HEAD
void *sendFile (void *arg);
void *receiveACK (void *arg);
=======
>>>>>>> 31852206e7b11f748545ca2706173a0b88d36c08

#endif
