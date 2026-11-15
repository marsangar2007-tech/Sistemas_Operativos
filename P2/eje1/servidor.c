#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define KTAM 20
#define KPUERTO 9999
#define KCOLA 5
#define KFICHERO "Google.html"

/* SERVIDOR CONCURRENTE.
    1- el padre espera conexiones en accept.
    2- cuando llega un cliente, crea un hijo con fork.
    3- el hijo atiende al cliente y termina.
    4- el padre sigue aceptando clientes.
*/

/* ---------- Utilidades ---------- */

/* Muestra un mensaje de error por la salida de error. */
static void avisar(const char *mensaje)
{
    fprintf(stderr, "%s\n", mensaje);
}

/* ---------- Preparacion del socket de escucha ---------- */

/* Abre el socket TCP. Devuelve el descriptor o -1. */
static int crear_socket()
{
    int escucha = socket(AF_INET, SOCK_STREAM, 0);

    if (escucha < 0)
        avisar("Error. No se pudo abrir el socket");
    return escucha;
}

/* Rellena la direccion del servidor (cualquier IP, puerto KPUERTO). */
static void preparar_direccion(struct sockaddr_in *direccion)
{
    bzero((char *) direccion, sizeof(*direccion));
    direccion->sin_port = htons(KPUERTO);
    direccion->sin_family = AF_INET;
    direccion->sin_addr.s_addr = INADDR_ANY;
}

/* Permite reutilizar el puerto sin esperar. */
static void permitir_reutilizar(int escucha)
{
    int activar = 1;

    setsockopt(escucha, SOL_SOCKET, SO_REUSEADDR, &activar, sizeof(activar));
}

/* Vincula el socket a la direccion. Devuelve 0 si va bien, -1 si falla. */
static int vincular_socket(int escucha, struct sockaddr_in *direccion)
{
    if (bind(escucha, (struct sockaddr *) direccion, sizeof(*direccion)) < 0) {
        avisar("Error no se pudo vincular el socket");
        return -1;
    }
    return 0;
}

/* Pone el socket a la escucha. Devuelve 0 si va bien, -1 si falla. */
static int activar_escucha(int escucha)
{
    if (listen(escucha, KCOLA) < 0) {
        avisar("error en listen");
        return -1;
    }
    return 0;
}

/* Deja el servidor listo para aceptar clientes.
   Devuelve el descriptor, -1 si falla la creacion/vinculacion
   y -2 si falla listen. */
static int preparar_escucha()
{
    struct sockaddr_in direccion;
    int escucha = crear_socket();

    if (escucha < 0)
        return -1;

    preparar_direccion(&direccion);
    permitir_reutilizar(escucha);

    if (vincular_socket(escucha, &direccion) < 0) {
        close(escucha);
        return -1;
    }
    if (activar_escucha(escucha) < 0) {
        close(escucha);
        return -2;
    }
    return escucha;
}

/* Configura las senales del proceso padre. */
static void configurar_senales()
{
    signal(SIGCHLD, SIG_IGN); /* evita hijos zombis */
    signal(SIGPIPE, SIG_IGN); /* evita que un cliente desconectado termine el proceso */
}

/* ---------- Transferencia del fichero ---------- */

/* Abre el fichero a transmitir. Devuelve el descriptor o -1. */
static int abrir_fichero()
{
    int origen = open(KFICHERO, O_RDONLY);

    if (origen < 0)
        avisar("error al abrir el fichero");
    return origen;
}

/* Envia exactamente 'total' bytes (write puede escribir menos).
   Devuelve 0 si todo bien, -1 si hubo error. */
static int enviar_bloque(int conexion, const char *datos, ssize_t total)
{
    ssize_t enviados = 0, n;

    while (enviados < total) {
        n = write(conexion, datos + enviados, total - enviados);
        if (n <= 0) {
            avisar("error al enviar al cliente");
            return -1;
        }
        enviados += n;
    }
    return 0;
}

/* Lee un bloque del fichero. Devuelve los bytes leidos (0 = fin, <0 = error). */
static ssize_t leer_bloque(int origen, char *bloque)
{
    ssize_t leidos = read(origen, bloque, KTAM - 1);

    if (leidos > 0)
        printf("he leido del fichero un bloque de %zd\n", leidos);
    return leidos;
}

/* Lee el fichero bloque a bloque y lo envia por la conexion.
   Devuelve el resultado de la ultima lectura. */
static ssize_t copiar_fichero(int origen, int conexion)
{
    char bloque[KTAM];
    ssize_t leidos = leer_bloque(origen, bloque);

    while (leidos > 0) {
        if (enviar_bloque(conexion, bloque, leidos) < 0)
            break;
        leidos = leer_bloque(origen, bloque);
    }
    return leidos;
}

/* Transmite el fichero completo por la conexion. */
static void transmitir_fichero(int conexion)
{
    ssize_t ultimo;
    int origen = abrir_fichero();

    if (origen < 0)
        return;

    ultimo = copiar_fichero(origen, conexion);
    if (ultimo < 0)
        avisar("error al leer el fichero");
    printf("ya he acabado, tamaño del ultimo bloque leido %zd\n", ultimo);
    close(origen);
}

/* ---------- Atencion a clientes ---------- */

/* Codigo del proceso hijo: atiende a un cliente y termina. */
static void atender_cliente(int escucha, int conexion)
{
    close(escucha); /* el hijo no acepta nuevas conexiones */
    printf("hijo acepta conexion\n");

    transmitir_fichero(conexion);

    printf("soy el que atendia la peticion, ya he acabado\n");
    close(conexion);
    fflush(stdout);
    _exit(0); /* el hijo no vuelve a accept */
}

/* Espera a un cliente. Devuelve la conexion o -1 si falla accept. */
static int esperar_cliente(int escucha)
{
    struct sockaddr_in cliente;
    socklen_t longitud = sizeof(cliente);
    int conexion = accept(escucha, (struct sockaddr *) &cliente, &longitud);

    if (conexion < 0)
        avisar("error al aceptar la conexion");
    return conexion;
}

/* Crea un hijo que atiende la conexion; el padre la cierra. */
static void lanzar_hijo(int escucha, int conexion)
{
    pid_t hijo;

    printf("aceptando conexion\n");
    fflush(stdout); /* antes de fork, para no duplicar mensajes */
    hijo = fork();

    if (hijo == 0) {
        atender_cliente(escucha, conexion);
    } else if (hijo > 0) {
        close(conexion); /* el padre no transmite el fichero */
    } else {
        avisar("error en fork");
        close(conexion);
    }
}

/* Bucle del padre: acepta clientes y crea un hijo por cada uno. */
static void bucle_servidor(int escucha)
{
    int conexion;

    while (1) {
        conexion = esperar_cliente(escucha);
        if (conexion >= 0)
            lanzar_hijo(escucha, conexion);
    }
}

/* ---------- Programa principal ---------- */

int main()
{
    int escucha = preparar_escucha();

    if (escucha == -2)
        return 1;
    if (escucha < 0)
        return 0;

    configurar_senales();
    bucle_servidor(escucha);
    return 0;
}
