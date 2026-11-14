#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define KTAM 20
#define KPUERTO 9999

/* CLIENTE.
    1- crea el socket.
    2- se conecta al servidor (IP recibida como argumento, puerto 9999).
    3- lee el contenido que envia el servidor y lo muestra por pantalla.
    Uso: ./client 127.0.0.1 (direccion de loopback, sobre la propia maquina)

   AF_INET es la familia de direcciones del socket (IPv4): al crear el
   socket hay que indicarla y solo se pueden usar direcciones de ese tipo.
*/

/* ---------- Utilidades ---------- */

/* Muestra un mensaje de error por la salida de error. */
static void avisar(const char *mensaje)
{
    fprintf(stderr, "%s\n", mensaje);
}

/* ---------- Conexion con el servidor ---------- */

/* Abre el socket TCP. Devuelve el descriptor o -1. */
static int crear_socket()
{
    int conexion = socket(AF_INET, SOCK_STREAM, 0);

    if (conexion < 0)
        avisar("Error. No se pudo crear socket");
    return conexion;
}

/* Rellena la direccion del servidor con la IP recibida y el puerto KPUERTO. */
static void preparar_direccion(struct sockaddr_in *destino, const char *ip)
{
    bzero((char *) destino, sizeof(*destino));
    destino->sin_port = htons(KPUERTO);
    destino->sin_family = AF_INET;
    destino->sin_addr.s_addr = inet_addr(ip);
}

/* Intenta conectar con el servidor. Devuelve 0 si va bien, -1 si falla. */
static int conectar_servidor(int conexion, struct sockaddr_in *destino)
{
    if (connect(conexion, (struct sockaddr *) destino, sizeof(*destino)) < 0) {
        avisar("Error. No se pudo conectar al socket");
        return -1;
    }
    return 0;
}

/* Crea el socket y lo conecta al servidor.
   Devuelve el descriptor listo para leer, o -1 si algo falla. */
static int abrir_conexion(const char *ip)
{
    struct sockaddr_in destino;
    int conexion = crear_socket();

    if (conexion < 0)
        return -1;

    preparar_direccion(&destino, ip);
    if (conectar_servidor(conexion, &destino) < 0) {
        close(conexion);
        return -1;
    }
    return conexion;
}

/* ---------- Recepcion del contenido ---------- */

/* Lee un bloque del servidor y lo muestra por pantalla.
   Devuelve los bytes leidos (0 = conexion cerrada, <0 = error). */
static ssize_t recibir_bloque(int conexion)
{
    char bloque[KTAM];
    ssize_t leidos = read(conexion, bloque, KTAM - 1); /* reservo un caracter para el \0 */

    if (leidos > 0) {
        bloque[leidos] = '\0';
        printf("%s", bloque); /* escribo en la pantalla */
    }
    return leidos;
}

/* Recibe todo el contenido hasta que el servidor cierra la conexion.
   Devuelve el resultado de la ultima lectura. */
static ssize_t recibir_contenido(int conexion)
{
    ssize_t leidos;

    do {
        leidos = recibir_bloque(conexion);
    } while (leidos > 0); /* read devuelve 0 al cerrarse la conexion */

    return leidos;
}

/* ---------- Programa principal ---------- */

int main(int argc, char *argv[])
{
    int conexion;

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <IP_del_servidor>\n", argv[0]);
        return 1;
    }

    conexion = abrir_conexion(argv[1]);
    if (conexion < 0)
        return 0;

    if (recibir_contenido(conexion) < 0)
        avisar("error al leer del socket");
    close(conexion);
    return 0;
}
