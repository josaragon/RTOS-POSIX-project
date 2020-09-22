
//This sensor will measure temperature-pressure(2) 

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
int simular = 0; //Simular 1 = extrem heat y anticyclon, Simular 2 = Cold y squalls. 5 - Require recalibration
mqd_t cola_sen1;
int apagado = 0;

/*Primary msgs structure*/
struct prim_msg{
 char tipo_sensor; //Sensor's type -> 1-TyP, 2-V_M, 3-AyH
 char num_sensor; //Sensor's name
};

/*Sensor's msgs structure*/
struct msg_handler{
  int valor1;	//First value given by sensor 
  int valor2;	//Second value given by sensor
  struct timespec ts;	//TimeStamp previous values 
  int pid; //PID sensor thread
};
struct msg_handler hand_attr;

/*Handler function that it will create random values*/
void manejador(int signo, siginfo_t *datos, void *pa_na)
{
  if (simular == 1)
  {
  temp = rand() % (50-35+1) + 35;
  pres = rand() % (1030-1024+1) + 1024;
  printf("Danger 1, temperature %d ºC and pressure %d mb\n", temp, pres);  
  }
  if (simular == 2)
  {
  temp = rand() % (5-(-5)+1) + (-5);
  pres = rand() % (1012-1007+1) + 1007;
  printf("Danger 2, temperature %d ºC and pressure %d mb\n", temp, pres); 
  }
  if (simular == 0)
  {
  temp = rand() % (35-5+1) + 5; 
  pres = rand() % (1024-1012+1) + 1012;  
  }
  hand_attr.valor1 = temp; 
  hand_attr.valor2 = pres; 
  clock_gettime(CLOCK_REALTIME, &hand_attr.ts); //TimeStamp
  hand_attr.pid = getpid(); //PID
  mq_send(cola_sen1, (char*)&hand_attr, sizeof(hand_attr), 0); //It will be sent by msgs queue "/sensor1" 
}

/*Handle function, the flag "apagado" allow us to shut down the system*/
void apagar(int sign){
  apagado = 1;
}

int main(void){
  struct mq_attr sen1_attr;
  
  if(mq_open("/sensor1", O_RDWR, S_IRWXU, &sen1_attr) == (mqd_t)(-1)){ //We check if "/sensor1" already exists by incorrect shut down of sensor_1 

  ///////////////*Primary queue*/////////////
  mqd_t cola_prim_1;

  struct prim_msg msg_sensor; //Primary msg structure
  
  struct mq_attr prim_attr;
  
  prim_attr.mq_maxmsg = 5;
  prim_attr.mq_msgsize = sizeof(msg_sensor);
  prim_attr.mq_flags = 0;
  
  /*Wait for primary msgs queue to be opened*/ 
  do{
    cola_prim_1 = mq_open("/colaprim", O_RDWR, S_IRWXU, &prim_attr);
  }
  while(cola_prim_1 == (mqd_t)(-1));
  
  printf("Primary queue opened, sensor 1\n");
    
  msg_sensor.tipo_sensor = 2; //Sensor, type 2 (Temperature and pressure) 
  msg_sensor.num_sensor = 1; //Sensor number
  msg_sensor.pid = getpid(); //PID from sensor process
    
  mq_send(cola_prim_1, (char*)&msg_sensor, sizeof(msg_sensor), 0); //Info will be sent to master
  mq_close(cola_prim_1); //Primary msgs queue is closed
  
  }
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
  /*Msgs queue to send info*/
  sen1_attr.mq_maxmsg = 7;
  sen1_attr.mq_msgsize = sizeof(hand_attr);
  sen1_attr.mq_flags = 0;
  

  /*Wait to get opened the queue to send the info*/ 
  do{
    cola_sen1 = mq_open("/sensor1", O_RDWR, S_IRWXU, &sen1_attr);
  }
  while(cola_sen1 == (mqd_t)(-1));
  
  printf("Queue sensor opened, sensor 1\n");
  
  //////////////////* Timer de 0.5 seg*/////////////////////
  sigset_t c_sensor1;
  struct sigaction accion1;
  struct timespec ciclo = { 0, 500000000L}; /* 0.5 seg */
  struct itimerspec tempo; /*Timer programming*/
  struct sigevent evento;
  struct sigaction sig_apagar; /*To shut down process sensor*/
  
  srand (time(NULL)); //Random seed to create sensor's values. 
  
  /* Configuracion de la máscara*/
  sigemptyset(&c_sensor1);
  sigaddset(&c_sensor1, SIGRTMIN); //Cyclical signal to send the info 
  sigaddset(&c_sensor1, SIGTERM); //Signal sent by master to shut down the sensor
  sigprocmask(SIG_BLOCK, &c_sensor1, NULL);
  
  /*Signal programming that will shut down the system*/
  sigemptyset(&sig_apagar.sa_mask);
  sig_apagar.sa_flags = 0;
  sig_apagar.sa_handler = apagar;
  sigaction(SIGTERM, &sig_apagar, NULL);
  
  /*Real time, with handler function */
  sigemptyset(&accion1.sa_mask);
  accion1.sa_flags = SA_SIGINFO;
  accion1.sa_sigaction = manejador; 
  sigaction(SIGRTMIN, &accion1, NULL);
  
  /* Sigevent as timer event*/
  evento.sigev_signo = SIGRTMIN;
  evento.sigev_notify = SIGEV_SIGNAL;
  
  /* Timer creation and configuration*/
  timer_create(CLOCK_REALTIME, &evento, &timer1);
  tempo.it_value = ciclo; /* Primer trigger de 0.5s */
  tempo.it_interval = ciclo; /*Cycle 0.5s */
  timer_settime(timer1, 0, &tempo, NULL);
  
  /* Unblock SIGTRMIN and SIGTERM signals*/
  sigprocmask(SIG_UNBLOCK, &c_sensor1, NULL);
  
  fflush(stdout);
  printf("Timer1 created\n");
  
  while(apagado == 0){
   scanf("%d",&simular);
  };
  
  /* If the code has reach this point, sensor1 is going to shut down*/
  
  mq_unlink("/sensor1");
  mq_close(cola_sen1);
  printf("Sensor1 shut down\n");
  
  return 0;
  
}