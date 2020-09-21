#define _POSIX_C_SOURCE 200112L
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define NUM_SENSORES 4

/*Primary msgs structure*/
struct prim_msg{
  char tipo_sensor; //Sensor's type -> 1-TyP, 2-V_M, 3-AyH
  char num_sensor; //Sensor's name
};

/*Sensor's msgs structure*/
struct msg_handler{
  int valor1;	//First value given by sensor 
  int valor2;	//Second value given by sensor
  struct timespec ts;	//TimeStamp de los valores anteriores
  int pid; //PID del proceso sensor
};

///////////////////////////////////////////////////////////////////////////

/*Global variables*/
int x = 0;
int sensor_t[NUM_SENSORES+1];	//The info from the type of the sensor and place it takes, will be saved here
struct msg_handler valores_sensores[NUM_SENSORES][10];	//All the values from sensor will be saved here
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/*Threads functions*/
void *h_msgsensor(void *p);
void *h_comandos(void *q);
void *h_catastrofe(void *r);

int main(void){
  //Local Variables
  int i;
  pthread_t *h;
  pthread_t *h_s; //Aquí se guardarán los indentificadores de los hilos para la lectura de sensores
  
  h = malloc(sizeof(pthread_t)*NUM_SENSORES+1); //Se reservan un hilo para cada sensor, mas el hilo de catástrofes 
  h_s = malloc(sizeof(pthread_t)*NUM_SENSORES+1); //Se reserva el mismo espacio que antes mas uno para identificar
  //mas adelante los hilos del sistema y poder apagarlos
  
  /*Creación de la cola de mensajes primaria*/
  
  struct prim_msg prim;	//Se define la estructura del mensaje primario
  mqd_t cola_prim;	//Cola primaria
  struct mq_attr prim_attr;	//Atributos de la cola
  struct mq_attr attr_num_msgs; //Lugar donde se verán los mensajes actuales de la cola primaria
  
  prim_attr.mq_maxmsg = 5;	//Cantidad de mensajes máximos en la cola primaria 
  prim_attr.mq_msgsize = sizeof(prim);	
  prim_attr.mq_flags = 0;
  
  mq_unlink("/colaprim");
  
  cola_prim = mq_open("/colaprim", O_RDWR | O_CREAT, S_IRWXU, &prim_attr);
  
  printf("Esperando a los mensajes de los sensores\n");
  
  sleep(2);
  
  /* Esperamos a que todos los sensores escriban el mensaje de su información, mirando cuantos mensajes actuales hay*/
  do{mq_getattr(cola_prim, &attr_num_msgs);}
  while(attr_num_msgs.mq_curmsgs != NUM_SENSORES);
  
  printf("Han llegado todos los mensajes\n");
  
  /* Configuración de las colas de mensaje de los sensores*/
  struct mq_attr sensor_attr;
  struct msg_handler hand_attr;
  sensor_attr.mq_maxmsg = 7;
  sensor_attr.mq_msgsize = sizeof(hand_attr);
  sensor_attr.mq_flags = 0;
  
  /* Colas de mensajes de los sensores*/
  mqd_t *msg_q;
  msg_q = malloc(sizeof(mqd_t)*NUM_SENSORES);
  
  memset(sensor_t, 0, sizeof(sensor_t));
  
  /* Se leen los mensajes oportunos mientras se crean los hilos para cada sensor*/
  for(i=0;i<NUM_SENSORES;i++){
    
    /* Se recibe la información de cada sensor */
    mq_receive(cola_prim, (char*)&prim, sizeof(prim), NULL);
    
    /* Se graba la información de cada sensor y su tipo en los hilos*/
    pthread_mutex_lock(&mut);
    while(sensor_t[0] > 0){pthread_cond_wait(&cond,&mut);}
    sensor_t[(int)prim.num_sensor] = (int)prim.tipo_sensor;
    sensor_t[0] = 1;
    pthread_mutex_unlock(&mut);
    
    /* Se crea el indentificador de las colas de mensajes dependiendo de su numero de sensor*/
    char buf[2];
    sprintf(buf,"%u", prim.num_sensor);
    char buf_sens[] = "/sensor";
    strcat(buf_sens, buf);
    
    /* Se crea la cola y se manda su identificador a los hilos que se crean*/
    msg_q[(int)prim.num_sensor] = mq_open(buf_sens, O_RDWR | O_CREAT, S_IRWXU, &sensor_attr);
    pthread_create(&h[(int)prim.num_sensor], NULL, h_msgsensor, &msg_q[(int)prim.num_sensor]);
    h_s[i] = h[(int)prim.num_sensor];
  }
  
  mq_unlink("/colaprim");
  mq_close(cola_prim);
  
  sleep(1);
  
  pthread_create(&h_s[NUM_SENSORES], NULL, h_catastrofe, NULL);

  pthread_t com;
  pthread_create(&com, NULL, h_comandos, (void *)h_s);
  
  pthread_join(com, NULL);
  
  for(i=0;i<NUM_SENSORES+1;i++){
  pthread_join(h[i], NULL);
  mq_close(msg_q[i]);
  }
  
  printf("Sistema apagado\n");
  
  return(0);
  
  //////////////////////////////////////////////////////////////////////////////////
}

// Hilos de manejo de mensajes de sensores
void *h_msgsensor(void *p){
    
  mqd_t *c = p;
  mqd_t cola_s = *c;
  int num,j,k;
  struct timespec now;
  struct timespec limite_t;
  int flag = 0;
  int buffer_flag[10] = {1,1,1,1,1,1,1,1,1,1};
  float dif;
  
  pthread_mutex_lock(&mut);
  while(sensor_t[0] < 1){pthread_cond_wait(&cond,&mut);}
  for(j=1;j<=NUM_SENSORES;j++){
    if(sensor_t[j] != 0){
      num = j;
    }
  }
  sensor_t[num] = 0;
  sensor_t[0] = 0;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mut);

  /* Buffers para que no haya pérdida de información por desborde de la cola */
  struct msg_handler *buffer;
  buffer = malloc(sizeof(*buffer)*10); //Se crea un buffer de dimensión 10
  
  sleep(1);
  
  struct timespec timeout;
 
  while(1)
  {
    for(j=0;j<10;j++){
       clock_gettime(CLOCK_REALTIME, &timeout);
       timeout.tv_sec = timeout.tv_sec + 2;
       if (mq_timedreceive(cola_s, (char*)&buffer[j], sizeof(*buffer), NULL, &timeout) < 0){
	 printf("Se ha perdido la comunicación con el sensor número %i\n", num);
	 break;
       }
      clock_gettime(CLOCK_REALTIME, &now);
      limite_t = now;
      dif = (now.tv_sec - limite_t.tv_sec) + (now.tv_nsec - limite_t.tv_nsec)*1e-9;//Se calcula la diferencia de tiempo entre dos instantes
      while (dif < 0.4 && flag == 0){ //Si no han pasado 400 ms aún o el flag de mutex ya unlocked
	if (pthread_mutex_trylock(&mut) == 0){ //Se intenta cerrar el mutex sin bloquear
	  valores_sensores[num-1][j] = buffer[j]; //Una vez conseguido, se guarda en la variable global el valor del sensor
	  for(k=0;k<j;k++){ //Los valores que no se hayan podido guardar por no haber conseguido cerrar el mutex se repasan y se guarda en caso positivo
	    if (buffer_flag[k] == 0){valores_sensores[num-1][k] = buffer[k];}
	  }
	  pthread_mutex_unlock(&mut); 
	  flag = 1;
	  memset(buffer_flag, 1, sizeof(buffer_flag)); //Se resetea el buffer donde está la información de los valores que no se han podido guardar en el momento
	}
	clock_gettime(CLOCK_REALTIME, &now);
	dif = (now.tv_sec - limite_t.tv_sec) + (now.tv_nsec - limite_t.tv_nsec)*1e-9; // Se resetea la diferencia para el siguiente ciclo
      }
      if (flag == 0){ //Si han pasado mas de 400ms, guardamos la posición en el buffer de qué valor no se ha guardado para cuando posteriormente, se consiga cerrar el mutex
	buffer_flag[j] = 0;
	fflush(stdout);
      }
      flag = 0;
    }
    pthread_testcancel();
  }
}

/* Hilo para el manejo de comandos*/
void *h_comandos(void *q){
  
  fflush(stdout);
  printf("Activacion del hilo de comandos, escriba -help- para ver los comandos disponibles\n");
  
  char c[20];
  int i, j, k;
  struct msg_handler valor_historico[NUM_SENSORES][10]; //Aquí se copiaran los valores en la variable global
  struct msg_handler actual;
  FILE *historico; //Fichero donde se guardaran los valores históricos mostrados por pantalla
  pthread_t *pid_h_sensores; //Estructura destino de los PIDs y pthreads_t para apagar el sistema
  pid_h_sensores = (pthread_t *)q;
  
  while(1){
    printf("Introduzca una instrucción\n");
    gets(c);
    fflush(stdout);

    if(strcmp(c, "help") == 0){
      printf("Para conocer los diez ultimos valores: historico\n");
      printf("Para pagar el sistema: apagar\n");
    }
    
    else if(strcmp(c, "historico") == 0){
      
      /* Copiamos los valores existentes en ese momento*/
      for (k=0;k<4;k++){
	for(i=0;i<10;i++){
	  valor_historico[k][i] = valores_sensores[k][i];
	}
      }
      
      /* Se ordenan por el TimeStamp con el algoritmo del ordenamiento por insercción*/
      for(k = 0; k < NUM_SENSORES; k++){
	for (i = 1; i < 10; i++) { 
        actual = valor_historico[k][i];
        j = i - 1; 
	  while (j >= 0 && (long)valor_historico[k][j].ts.tv_sec > (long)actual.ts.tv_sec) { 
            valor_historico[k][j + 1] = valor_historico[k][j]; 
            j = j - 1; 
	  } 
        valor_historico[k][j + 1] = actual; 
	}
      }
      
      /* Se imprimen por pantalla y en un archivo*/
      struct tm *aux;
      historico = fopen("historico.txt","a");
      
      for (k = 0; k < NUM_SENSORES; k++){
	for(i = 0; i < 10; i++){
	  aux = localtime(&valor_historico[k][i].ts.tv_sec);
	  fflush(stdout);
	  printf("Cola %i\tValor1: %i\tValor2: %i\tTimeStamp: %s\tPID: %i\n",k+1, valor_historico[k][i].valor1, valor_historico[k][i].valor2, asctime(aux), valor_historico[k][i].pid);
	  fprintf(historico,"Cola %i\tValor1: %i\tValor2: %i\tTimeStamp: %s\tPID: %i\n",k+1, valor_historico[k][i].valor1, valor_historico[k][i].valor2, asctime(aux), valor_historico[k][i].pid);
	}
      fflush(stdout);
      printf("\n\n\n\n");
      fprintf(historico,"\n\n\n\n");
      }
      fclose(historico);
    }
    
    else if(strcmp(c, "apagar") == 0){
      break;
    }
    
    else{
     printf("Comando no reconocido, use -help- para ver los comandos disponibles\n");
    }
  }
  
  /* Si se ha enviado -acabar- por la consola*/
  
  for(k = 0; k < NUM_SENSORES+1; k++){
    pthread_cancel(pid_h_sensores[k]);
  }
  
  for(k = 0; k < NUM_SENSORES; k++){
    kill((pid_t)valores_sensores[k][1].pid, SIGTERM); //Problemas con timer_delete    
  }
        
  return 0;
}


/* Hilo de catastrofes*/
void *h_catastrofe(void *r){
  
  int lv1_sensor[NUM_SENSORES];
  int lv2_sensor[NUM_SENSORES];
  struct timespec delay;
  delay.tv_sec = 0;
  delay.tv_nsec = 300*1000000L;

  int v,n;
    
  while(1){
    
    for (n = 0; n < 10; n++){
      for (v = 0; v < NUM_SENSORES; v++){
	  lv1_sensor[v] = valores_sensores[v][n].valor1;
	  lv2_sensor[v] = valores_sensores[v][n].valor2;
      }
      if((lv1_sensor[1] > 31 || lv1_sensor[2] > 31) && lv2_sensor[3] < 7){
	printf("TERREMOTO\n"); 
      }
      else if(lv1_sensor[0] > 35 && lv2_sensor[0] > 1024 && lv1_sensor[3] < 61){
	printf("CALOR EXTREMO\n"); 
      }
      else if((lv1_sensor[0] > 35 && lv2_sensor[0]) && (lv1_sensor[3] > 61 && lv2_sensor[3] < 7)){
	printf("TORMENTA DE ARENA\n"); 
      }
      else if(lv1_sensor[3] < 61 && lv2_sensor[3] > 7 && !(lv1_sensor[1] > 31  ||  lv1_sensor[2] > 31)){
	printf("DILUVIO/GRANIZO\n");
      }
      else if((lv1_sensor[1] > 31  ||  lv1_sensor[2] > 31) && lv2_sensor[3] > 7){
	printf("MAREMOTO\n"); 
      }
      nanosleep(&delay,NULL);
      pthread_testcancel();
    }
  } 
  return 0;
}