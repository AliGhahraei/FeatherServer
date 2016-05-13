/*
  Feather Server
  Copyright (C) 2016 by Alexandro Cebrián Mancera and Ali Ghahraei Figueroa

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.


  Servidor simple que utiliza TCP. Es capaz de proveer los archivos HTML a el
  cliente.
  Su uso se encuentra documentado en "servidor --help".
*/


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <argp.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>


#define NAME = "Feather Server"


/* Definimos que una dirección es una estructura del tipo sockaddr_in */
typedef struct sockaddr_in direccion;


/* Démosle color a esto */
#define VERDE "\x1B[32m"
#define FIN "\x1B[0m"
#define AZUL "\x1B[36m"
#define ROJO "\x1B[31m"
#define BLANCO "\x1B[37m"
#define AMARILLO "\x1B[33m"


/*
  El socket principal se guarda como variable global para poderlo cerrar
  cuando llegue una señal. También se tienen valores bandera para saber si se
  quiere un puerto aleatorio o que no se impriman mensajes de debuggeo y, por
  supuesto, tenemos una variable para el puerto.
*/
int sock = 0, quiet = 0, aleat = 0, puerto = 0;


/*Cosas de argp (para parsear argumentos)*/

//Versión y correo de los autores
const char *argp_program_version = "SSW 0.1";
const char *argp_program_bug_address = "aligf94@gmail.com or alex.cebrianm@gmail.com";

//Descripción básica del programa
static char documentacion[] = "Feather Server -- ultra-minimalist server";

//Opciones aceptadas
static struct argp_option opciones[] = {
  {"port",   'p', "PORT", 0, "Provide a port number to connect to"},
  {"random", 'r', 0,      0, "Ask for connection to a random port" },
  {"quiet",  'q', 0,      0, "Keep quiet" },
  { 0 }
};


/* 
   Función para parsear argumentos que es llamada por argp automáticamente
   para cada opción y cada argumento provenientes del shell. 
   argumento es la opción proveniente de la línea de comandos, arg es una 
   estructura para comunicarse con main (no usada en este programa) y estado
   contiene el número que tiene el argumento que está siendo leído (además de
   otras cosas no utilizadas aquí)
*/
static error_t parseo(int argumento, char *arg, struct argp_state *estado){
    switch (argumento){
    case 'q':
      //Si leemos una q, el usuario no quiere cosas de debuggeo
      quiet = 1;
      break;
    case 'r':
      //r quiere decir random
      aleat = 1;
      break;
    case 'p':
      //p quiere decir puerto. Lo guardamos como entero
      puerto = atoi(arg);
      break;
    case ARGP_KEY_ARG:
      //Se entra en esta opción cuando se está procesando un argumento no
      //opcional. Debido a que no recibimos argumentos no opcionales,
      //mostramos el uso correcto y salimos
      argp_usage(estado);
      break;
    case ARGP_KEY_END:
      //Se entra en esta opción cuando procesamos el final de la lista no
      //opcional (algo equivalente al caracter nulo en una cadena). Suele
      //usarse para mostrar uso si no se pasó el número mínimo de argumentos,
      //pero como aceptamos que no nos manden nada solo llamamos a break
      break;
    default:
      //Algo salió... Muy mal. Que el usuario lo sepa
      return ARGP_ERR_UNKNOWN;
    }

    //Si el usuario quiere un puerto específico y además uno aleatorio, no
    //furula y se lo hacemos saber con un mensaje de error y el uso
    if(puerto && aleat){
      fprintf(stderr, "Error: port and random options may not be called together\n");
      argp_usage(estado);
    }

    //Si todo parece estar bien, retornamos que no hubo error
    return 0;
}


/* Función para fracasar. Toma un mensaje, lo imprime y sale con código de
 * error 1.
 */
void falla(char *mensaje){
  fprintf(stderr, "%s%s%s\n", ROJO,mensaje,FIN);
  exit(1);
}


/* Función para verificar si un entero es negativo. Sale si esto es verdad */
void verifica(int verificado,char *mensaje){
  if(verificado < 0){
    falla(mensaje);
  }
}


/* Función para leer el archivo llamado "nombreArchivo" */
char *leeArchivo(char *nombreArchivo){
  //Abrimos el archivo
  FILE *archivo = fopen(nombreArchivo,"r");

  //Creamos una variable para guardar el contenido del archivo
  char *contenido = (char*)malloc(sizeof(char)*1000000);

  if(archivo){
    //Si el archivo se pudo abrir, leemos caracter por caracter y vamos
    //guardando en contenido hasta toparnos con EOF
    int caracter, i=0;
    while((caracter = getc(archivo)) != EOF){
      contenido[i] = caracter;
      i++;
    }

    //Añadimos el caracter nulo al final de la cadena
    contenido[i+1]='\0';

    //Cerramos el archivo
    fclose(archivo);
  }else{
    //Si el archivo no se pudo abrir, el contenido es nulo (y la función que)
    //llamó a esta se debe encargar de manejar esta situación
    contenido = NULL;
  }

  return contenido;
}


/* Función para obtener el nombre del archivo de una cadena de texto */
char *obtenNombreArchivo(char *texto){
  //Creamos una cadena para el nombre y un entero para el índice actual
  char *nombre = (char*)malloc(sizeof(char)*16384);
  int indiceSiguienteNombre = 0;
  
  //Empieza desde el cuarto caracter (Es decir, luego de "GET ") y lee hasta
  //el siguiente caracter de espacio para obtener el nombre. Ve guardando los
  //caracteres en la cadena del nombre
  int i = 4;
  while(texto[i] != ' '){
    nombre[indiceSiguienteNombre] = texto[i];
    i++;
    indiceSiguienteNombre++;
  }

  //Pon el caracter nulo al final
  nombre[indiceSiguienteNombre+1] = '\0';

  return nombre;
}


/* Función para manejar una petición en un socket ya aceptado */
void manejaPeticion(int socketAceptado){
  //Se lee el mensaje del cliente en un buffer y se verifica si todo funcionó
  char mensaje [1024];
  int caracteresLeidos = read(socketAceptado, mensaje, 1024);
  verifica(caracteresLeidos,"No pudo leerse el mensaje del cliente");
	
  //Se muestra el mensaje del cliente
  if(!quiet)
    printf("\n%sPeticion:\n%s%s\n",AZUL,FIN,mensaje);

  //Obtenemos el nombre del archivo solicitado
  char *nombreArchivo = obtenNombreArchivo(mensaje);

  //Creamos variable para los contenidos del archivo y para el nombre completo
  char *contenido, *nombreCompletoArchivo = (char*)malloc(sizeof(char)*16384);

  //Copiamos la cadena del directorio al nombre completo
  char *directorio = "html\0";
  strcpy(nombreCompletoArchivo,directorio);

  //Si la cadena tiene un solo caracter ('/'), cargamos el default. Si no,
  //cargamos el archivo pedido
  if(nombreArchivo[1] == '\0'){
    strcat(nombreCompletoArchivo,"/index.html\0");
  }else{
    strcat(nombreCompletoArchivo,nombreArchivo);
  }
  
  //Cargamos el contenido
  contenido = leeArchivo(nombreCompletoArchivo);
  
  //Creamos una respuesta
  char *respuesta = (char*)malloc(sizeof(char)*16384);

  if(contenido){
    //Si hay contenido, lo añadimos a la respuesta luego del encabezado
    strcpy(respuesta, "HTTP/1.1 200 OK\r\n\n\0");
    strcat(respuesta, contenido);
  }else{
    //Si no hay contenido, generamos un hermoso 404
    char *contenidoError = leeArchivo("error");
    strcpy(respuesta, "HTTP/1.1 404 Not Found\r\n\n\0");
    strcat(respuesta, contenidoError);

    free(contenidoError);
    if(!quiet)
      printf("%sNo se encontró el archivo%s\n",ROJO,FIN);
  }

  //Escribimos la respuesta por medio del socket
  int caracteresEscritos = write(socketAceptado, respuesta ,strlen(respuesta));
  if(!quiet)
    printf("\n%sRespuesta:\n%s%s\n",BLANCO,FIN,respuesta);
  verifica(caracteresEscritos, "No pudo escribirse la respuesta en el socket");

  //Liberamos toda la memoria
  free(contenido);
  free(nombreArchivo);
  free(nombreCompletoArchivo);
  free(respuesta);
}


/* Función para manejar señales */
void noMorire(int sig){
  printf("\n\n%sRecibida la señal %d. El servidor finalizará\n",VERDE,sig);
  shutdown(sock,2);
  close(sock);
  printf("Nuestro amigo errno dice: %s%s\n",strerror(errno),FIN);
  exit(0);
}


/*Función que espera a una conexión entrante y la acepta. */
void aceptaConexion(int socketViejo){
  //Definimos cómo manejar la señal de SIGINT
  (void) signal(SIGINT, noMorire);
  
  //Se crea la variable que guarda la dirección del cliente y otra que guarda
  //el tamaño de esta estructura
  socklen_t largoCliente = sizeof(direccion);
  direccion cliente;

  //Creamos un conjunto de descriptores, lo limpiamos y añadimos al del
  //socket. Todo esto para luego poder usar select y que los puertos no den
  //errores con la señal SIGINT, cosa que pasa si solo usamos accept
  fd_set sockets;
  FD_ZERO(&sockets);
  FD_SET(socketViejo, &sockets);

  //Esta llamada bloquea la ejecución hasta que hay un cliente intentando escribir
  verifica(select(socketViejo+1, &sockets, NULL, NULL, NULL),"Una petición tuvo error");

  //Se acepta la conexión, guardando la dirección en la variable cliente
  int socketAceptado = accept(socketViejo, (struct sockaddr *) &cliente, &largoCliente);
  verifica(socketAceptado,"No pudo aceptarse la conexión");	

  //Se forkea y verifica
  int pid = fork();
  verifica(pid,"No se pudo crear un nuevo proceso para manejar la petición");

  //Que el hijo maneje la petición. El padre solo cerrará el socket y
  //regresará a la función abreConexion para recibir más peticiones
  if(pid==0){
    //Cerramos el socket viejo, quitamos a ese descriptor del conjunto que
    //creamos
    close(socketViejo);
    FD_CLR(socketViejo, &sockets);

    //Manejamos la petición y cerramos el socket
    manejaPeticion(socketAceptado);
    close(socketAceptado);

    //Salimos
    if(!quiet)
      printf("%sPetición manejada con éxito\nEsperando al cliente...%s\n",AMARILLO,FIN);
    exit(0);
  }else
    //El padre solo cierra al nuevo
    close(socketAceptado);
}


/*Función que pone a un socket a escuchar en el puerto proporcionado. */
void abreConexion(int puerto){  
  //Abrimos una conexión del dominio de internet, con socket de stream y
  //que usa el default para el tipo de socket (TCP en este caso para
  //streams). Se guarda el descriptor del socket en "sock" y se verifica
  //que se haya creado exitosamente.
  sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK,0);	
  verifica(sock,"No se pudo crear un nuevo socket para el servidor");

  //Que el puerto sea usable aunque el programa termine (gracias a SO_REUSEADDR)
  setsockopt(sock,SOL_SOCKET,SO_REUSEADDR, &(int){1}, sizeof(int));

  //Se crea la variable donde se guardará la dirección del servidor. Esta
  //debe inicializarse a 0
  direccion servidor;
  memset(&servidor, 0, sizeof(servidor));
  servidor.sin_family = AF_INET; // Será del dominio de internet
  servidor.sin_port = htons(puerto); //Tendrá el puerto convertido a orden de red
  servidor.sin_addr.s_addr = INADDR_ANY; //Tendrá como IP la de esta computadora

  //Enlazamos al socket con la dirección (casteándola primero) y salimos si
  //no es posible
  int resEnlace = bind(sock, (struct sockaddr*) &servidor, sizeof(servidor));
  verifica(resEnlace,"Error en el enlace. Intente con otro puerto, por favor");

  //El socket escucha con solo un cliente en espera permitido
  listen(sock, 1);
  printf("%sEsperando al cliente...%s\n",AMARILLO,FIN);

  //Se esperan conexiones provenientes de un cliente y se aceptan. Al
  //aceptarlas el hijo, seguimos esperando conexiones gracias al loop
  while(1){
    aceptaConexion(sock);
  }
}


/* Función principal. */
int main(int argc, char *argv[]){
  //Creamos la estructura que necesita argp para parsear argumentos
  struct argp argp = { opciones, parseo, NULL, documentacion };
  
  //Empezamos por leer los argumentos provenientes de la línea de comandos
  argp_parse(&argp, argc, argv, 0, 0, 0);
  
  if(aleat){
    //Si queremos un puerto aleatorio, damos uno entre 2000 y 65535
    srand(time(NULL));
    puerto = (rand() % 63536) + 2000;
  }else if(!puerto){
    //Si no queremos un aleatorio y no se proveyó un puerto, el default es
    //8000
    puerto = 8000;
  }

  printf("%sSe abre la conexión al puerto %d%s\n",VERDE,puerto,FIN);	
  abreConexion(puerto);

  return 0;
}
