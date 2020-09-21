
//Este sensor será de temperatura-presión (2)

#define _POSIX_C_SOURCE 199506L
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <stdlib.h>

timer_t timer1;
int temp;
int pres;
int simular = 0; //Simular 1 = calor extrema y anticiclón, Simular 2 = Frio y borrascas. 5 - Falta calibrar
mqd_t cola_sen1;
int apagado = 0;

/* Estructura del mensaje primario*/
struct prim_msg{
 char tipo_sensor; //Tipo de sensor ->  1-V_M, 2-TyP, 3-HyA
 char num_sensor; //Número del sensor
};

/* Estructura de los mensajes de los sensores*/
struct msg_handler{
  int valor1;
  int valor2;
  struct timespec ts;
  int pid; //PID del proceso sensor que se mandará al maestro
};
struct msg_handler hand_attr;

/* Función manejadora que creará valores aleatorios */
void manejador(int signo, siginfo_t *datos, void *pa_na)
{
  if (simular == 1)
  {
  temp = rand() % (50-35+1) + 35;
  pres = rand() % (1030-1024+1) + 1024;
  printf("Peligro 1, temperatura %d ºC y presion %d milibares\n", temp, pres);  
  }
  if (simular == 2)
  {
  temp = rand() % (5-(-5)+1) + (-5);
  pres = rand() % (1012-1007+1) + 1007;
  printf("Peligro 2, temperatura %d ºC y presion %d milibares\n", temp, pres); 
  }
  if (simular == 0)
  {
  temp = rand() % (35-5+1) + 5; 
  pres = rand() % (1024-1012+1) + 1012;  
  }
  hand_attr.valor1 = temp; //Se recoge el valor de temperatura creado
  hand_attr.valor2 = pres; //Se recoge el valor de presión creado
  clock_gettime(CLOCK_REALTIME, &hand_attr.ts); //Se recoge el TimeStamp
  hand_attr.pid = getpid(); //Se recoge el pid 
  mq_send(cola_sen1, (char*)&hand_attr, sizeof(hand_attr), 0); //Se envía por la cola de mensajes de "/sensor1"
}

/* Función manejadora simple, cuyo flag "apagado" permite apagar correctamente el sistema*/
void apagar(int sign){
  apagado = 1;
}

int main(void){
  struct mq_attr sen1_attr;
  
  if(mq_open("/sensor1", O_RDWR, S_IRWXU, &sen1_attr) == (mqd_t)(-1)){ //Se comprueba si existe ya la cola "/sensor1" por un mal apagado del sistema sensor_1
  
  ///////////////*Cola Primaria*/////////////
  mqd_t cola_prim_1;

  struct prim_msg msg_sensor; //Se define la estructura del mensaje primario
  
  struct mq_attr prim_attr;
  
  prim_attr.mq_maxmsg = 5;
  prim_attr.mq_msgsize = sizeof(msg_sensor);
  prim_attr.mq_flags = 0;
  
  /*Esperamos a que se abra la cola de mensajes primaria*/
  do{
    cola_prim_1 = mq_open("/colaprim", O_RDWR, S_IRWXU, &prim_attr);
  }
  while(cola_prim_1 == (mqd_t)(-1));
  
  printf("Cola primaria abierta, sensor 1\n");
    
  msg_sensor.tipo_sensor = 2; //Se define que el sensor es de tipo dos (Temperatura y Presión)
  msg_sensor.num_sensor = 1; //El número de sensor
  //msg_sensor.pid = getpid(); //Se recoge el pid del proceso sensor
    
  mq_send(cola_prim_1, (char*)&msg_sensor, sizeof(msg_sensor), 0); //Se envía toda esa información al maestro
  mq_close(cola_prim_1); //Se cierra la cola de mensajes primaria
  
  }
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
  /* Cola de mensajes de envío de la información*/
  sen1_attr.mq_maxmsg = 7;
  sen1_attr.mq_msgsize = sizeof(hand_attr);
  sen1_attr.mq_flags = 0;
  

  /* Esperamos a que se abra la cola para comenzar el envio de información*/
  do{
    cola_sen1 = mq_open("/sensor1", O_RDWR, S_IRWXU, &sen1_attr);
  }
  while(cola_sen1 == (mqd_t)(-1));
  
  printf("Cola sensor abierta, sensor 1\n");
  
  //////////////////* Creación del timer de 0.5 seg*/////////////////////
  sigset_t c_sensor1;
  struct sigaction accion1;
  struct timespec ciclo = { 0, 500000000L}; /* 0.5 segundos */
  struct itimerspec tempo; /* Programacion del temporizador */
  struct sigevent evento;
  struct sigaction sig_apagar; /* Para apagar el proceso sensor*/
  
  srand (time(NULL)); //Semilla seudoaleatoria para los valores posteriores de sensores
  
  /* Configuracion de la máscara*/
  sigemptyset(&c_sensor1);
  sigaddset(&c_sensor1, SIGRTMIN); //Señal que será cíclica para el envío de la información
  sigaddset(&c_sensor1, SIGTERM); //Señal que llegará del maestro para apagar el sistema sensor
  sigprocmask(SIG_BLOCK, &c_sensor1, NULL);
  
  /* Programacion de la señal que apagará el sistema*/
  sigemptyset(&sig_apagar.sa_mask);
  sig_apagar.sa_flags = 0;
  sig_apagar.sa_handler = apagar;
  sigaction(SIGTERM, &sig_apagar, NULL);
  
  /* La programo de tiempo real, con manejador */
  sigemptyset(&accion1.sa_mask);
  accion1.sa_flags = SA_SIGINFO;
  accion1.sa_sigaction = manejador; 
  sigaction(SIGRTMIN, &accion1, NULL);
  
  /* Preparacion sigevent para ponerla como evento de temporizador */
  evento.sigev_signo = SIGRTMIN;
  evento.sigev_notify = SIGEV_SIGNAL;
  
  /* Creacion y configuracion del timer*/
  timer_create(CLOCK_REALTIME, &evento, &timer1);
  tempo.it_value = ciclo; /* Primer disparo de 0.5s */
  tempo.it_interval = ciclo; /* Es periodico también de 0.5s */
  timer_settime(timer1, 0, &tempo, NULL);
  
  /* Desbloqueamos las señales SIGRTMIN y SIGTERM*/
  sigprocmask(SIG_UNBLOCK, &c_sensor1, NULL);
  
  fflush(stdout);
  printf("Timer1 creado\n");
  
  /* Bucle que permite escribir como simular los valores y esperar al apagado correcto del sistema*/
  while(apagado == 0){
   scanf("%d",&simular);
  };
  
  /* Si el programa llega a este punto es que se ha pedido apagar el sensor1*/
  
  //timer_delete(&timer1);
  mq_unlink("/sensor1");
  mq_close(cola_sen1);
  printf("Sensor 1 apagado\n");
  
  return 0;
  
}