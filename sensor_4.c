 
//Este sensor será de humedad-anemómetro(3)

#define _POSIX_C_SOURCE 199506L
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <mqueue.h>
#include <stdlib.h>
#include <sys/stat.h>

timer_t timer4;
int viento;
int lluvia;
int simular = 0; //Simular 1: tornado, Simular 2: lluvia torrencial/granizo . Simular 5 - Falta calibrar
mqd_t cola_sen4;
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
  viento = rand() % (200-61+1) + 61;
  lluvia = rand() % (7-0+1) + 0; 
  printf("Peligro 1, viento %d km/h y lluvia %d mm/h\n", viento, lluvia);  
  }
  if (simular == 2)
  {
  viento = rand() % (61-0+1) + 0;
  lluvia = rand() % (120-7+1) + 7;
  printf("Peligro 2, viento %d km/h y lluvia %d mm/h\n", viento, lluvia); 
  }
  if (simular == 0)
  {
  viento = rand() % (61-0+1) + 0; 
  lluvia = rand() % (7-0+1) + 0;  
  } 
  hand_attr.valor1 = viento; //Se recoge el valor de viento creado
  hand_attr.valor2 = lluvia; //Se recoge el valor de lluvia creado
  clock_gettime(CLOCK_REALTIME, &hand_attr.ts); //Se recoge el TimeStamp
  hand_attr.pid = getpid(); //Se recoge el pid 
  mq_send(cola_sen4, (char*)&hand_attr, sizeof(hand_attr), 0); //Se envía por la cola de mensajes de "/sensor4"
}

/* Función manejadora simple, cuyo flag "apagado" permite apagar correctamente el sistema*/
void apagar(int sign){
  apagado = 1;
}

int main(void){
  struct mq_attr sen4_attr;
  
  if(mq_open("/sensor4", O_RDWR, S_IRWXU, &sen4_attr) == (mqd_t)(-1)){//Se comprueba si existe ya la cola "/sensor2" por un mal apagado del sistema sensor_4
  
  ///////////////*Cola Primaria*/////////////
  mqd_t cola_prim_4;

  struct prim_msg msg_sensor; //Se define la estructura del mensaje primario
  
  struct mq_attr prim_attr;
  
  prim_attr.mq_maxmsg = 5;
  prim_attr.mq_msgsize = sizeof(msg_sensor);
  prim_attr.mq_flags = 0;
  
  /*Esperamos a que se abra la cola de mensajes primaria*/
  do{
    cola_prim_4 = mq_open("/colaprim", O_RDWR, S_IRWXU, &prim_attr);
  }
  while(cola_prim_4 == (mqd_t)(-1));
  
  printf("Cola primaria abierta, sensor 4\n");
 
  msg_sensor.tipo_sensor = 3; //Se define que el sensor es de tipo uno (Velocidad - Movimiento)
  msg_sensor.num_sensor = 4; //El número del sensor
  //msg_sensor.pid = getpid(); //Se recoge el pid del proceso sensor
    
  mq_send(cola_prim_4, (char*)&msg_sensor, sizeof(msg_sensor), 0); //Se envía toda esa información al maestro
  mq_close(cola_prim_4); //Se cierra la cola de mensajes primaria
  
  }
  ///////////////////////////////////////////////////////////////////////////////////////
  
  /* Cola de mensajes de envío de la información*/
  
  sen4_attr.mq_maxmsg = 7;
  sen4_attr.mq_msgsize = sizeof(hand_attr);
  sen4_attr.mq_flags = 0;
  
  /* Esperamos a que se abra la cola para comenzar el envio de información*/
  do{
    cola_sen4 = mq_open("/sensor4", O_RDWR, S_IRWXU, &sen4_attr);
  }
  while(cola_sen4 == (mqd_t)(-1));
  
  printf("Cola sensor abierta, sensor 4\n");
  
  //////////////////* Creación del timer de 0.5 seg*/////////////////////
  sigset_t c_sensor4;
  struct sigaction accion4;
  struct timespec ciclo = { 0, 500000000L}; /* 0.5 segundos */
  struct itimerspec tempo; /* Programacion del temporizador */
  struct sigevent evento;
  struct sigaction sig_apagar; /* Para apagar el proceso sensor*/
  
  srand (time(NULL)); //Semilla seudoaleatoria para los valores posteriores de sensores
  
  /* Configuracion de la máscara*/
  sigemptyset(&c_sensor4);
  sigaddset(&c_sensor4, SIGRTMIN+3); //Señal que será cíclica para el envío de la información
  sigaddset(&c_sensor4, SIGTERM); //Señal que llegará del maestro para apagar el sistema sensor
  sigprocmask(SIG_BLOCK, &c_sensor4, NULL);
  
  /* Programacion de la señal que apagará el sistema*/
  sigemptyset(&sig_apagar.sa_mask);
  sig_apagar.sa_flags = 0;
  sig_apagar.sa_handler = apagar;
  sigaction(SIGTERM, &sig_apagar, NULL);
  
  /* La programo de tiempo real, con manejador */
  sigemptyset(&accion4.sa_mask);
  accion4.sa_flags = SA_SIGINFO;
  accion4.sa_sigaction = manejador; 
  sigaction(SIGRTMIN+3, &accion4, NULL);
  
  /* Preparacion sigevent para ponerla como evento de temporizador */
  evento.sigev_signo = SIGRTMIN+3;
  evento.sigev_notify = SIGEV_SIGNAL;
  
  /* Creacion y configuracion del timer*/
  timer_create(CLOCK_REALTIME, &evento, &timer4);
  tempo.it_value = ciclo;         /* Disparo tras interv */
  tempo.it_interval = ciclo;        /*Es periodico */
  timer_settime(timer4, 0, &tempo, NULL);
  
  /* Desbloqueamos las señales SIGRTMIN y SIGTERM*/
  sigprocmask(SIG_UNBLOCK, &c_sensor4, NULL);
  
  fflush(stdout);
  printf("Timer4 creado\n");
  
  /* Bucle que permite escribir como simular los valores y esperar al apagado correcto del sistema*/
  while(apagado == 0){
   scanf("%d",&simular);
  };
  
  /* Si el programa llega a este punto es que se ha pedido apagar el sensor2*/
  
  //timer_delete(&timer4);
  mq_unlink("/sensor4");
  mq_close(cola_sen4);
  printf("Sensor 4 apagado\n");
  
  return 0;
  
}