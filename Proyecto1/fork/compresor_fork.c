#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/time.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h> // Incluir fork. Solo funciona en linux
#include <sys/wait.h>
#include <dirent.h> //alphjasort
#include "funciones_arboles.h"
#include <limits.h>
// #include <linux/limits.h>
#include <sys/mman.h> //mmap

#define MAX_RUTA 256 // Longitud maxima de la ruta.

typedef struct
{
    char nombre[256];            // Nombre del archivo original
    unsigned int tam_comprimido; // Tamaño en bytes del archivo comprimido
    unsigned char bits_finales;  // Bits útiles en el último byte
} MetaArchivo;

// Ordenar los nombres de archivos alfabeticamente
// Sirve para que independientemente de el orden en que se lean los archivos, siempre se genera la misma metada que se genere en la version serial.
int comparar_meta_por_nombre(const void *a, const void *b)
{
    const MetaArchivo *ma = (const MetaArchivo *)a;
    const MetaArchivo *mb = (const MetaArchivo *)b;
    return strcmp(ma->nombre, mb->nombre);
}

// Función para comparar nodos, se compara que aquellos con mayor frecuencia vayan antes en el arreglo.
int comparar_nodos(const void *a, const void *b)
{
    NodoArbol *nodoA = *((NodoArbol **)a);
    NodoArbol *nodoB = *((NodoArbol **)b);

    return nodoA->frecuencia - nodoB->frecuencia;
}

void guardarArchivoComprimido(const char *nombreArchivo, Ruta *rutas, int num_rutas, const char *extension, const char *ultimoByte)
{
    char nombreComprimido[256];
    char nombreTabla[256];

    snprintf(nombreComprimido, sizeof(nombreComprimido), "%s.huff", nombreArchivo);
    snprintf(nombreTabla, sizeof(nombreTabla), "%s.table", nombreArchivo);

    FILE *archivoOriginal = fopen(nombreArchivo, "rb");
    if (!archivoOriginal)
    {
        perror("Error al abrir el archivo original");
        return;
    }

    FILE *archivoComprimido = fopen(nombreComprimido, "wb");
    if (!archivoComprimido)
    {
        perror("Error al crear el archivo comprimido");
        fclose(archivoOriginal);
        return;
    }

    char *codigos[256] = {0};
    for (int i = 0; i < num_rutas; i++)
    {
        codigos[rutas[i].byte] = rutas[i].ruta;
    }

    char byteAEscribir[MAX_RUTA] = "";
    int c;
    while ((c = fgetc(archivoOriginal)) != EOF)
    {
        strcat(byteAEscribir, codigos[c]);

        while (strlen(byteAEscribir) >= 8)
        {
            char byteBin[9];
            strncpy(byteBin, byteAEscribir, 8);
            byteBin[8] = '\0';

            unsigned char byteFinal = (unsigned char)strtol(byteBin, NULL, 2);
            fputc(byteFinal, archivoComprimido);

            memmove(byteAEscribir, byteAEscribir + 8, strlen(byteAEscribir) - 7);
        }
    }

    int bitsRestantes = strlen(byteAEscribir);
    FILE *archivoTabla = fopen(nombreTabla, "w");
    if (!archivoTabla)
    {
        perror("Error al crear el archivo de la tabla");
        fclose(archivoOriginal);
        fclose(archivoComprimido);
        return;
    }

    // Guardar la tabla de codigos
    for (int i = 0; i < num_rutas; i++)
    {
        fprintf(archivoTabla, "%s,%d\n", rutas[i].ruta, rutas[i].byte);
    }
    fprintf(archivoTabla, "%s\n", extension);

    fprintf(archivoTabla, "%s", byteAEscribir);
    fclose(archivoTabla);
    if (bitsRestantes > 0)
    {
        while (strlen(byteAEscribir) < 8)
        {
            strcat(byteAEscribir, "0");
        }

        unsigned char byteFinal = (unsigned char)strtol(byteAEscribir, NULL, 2);
        fputc(byteFinal, archivoComprimido);
    }
    else
    {
        fprintf(archivoComprimido, "No"); // No he testeado este escenario
    }

    fclose(archivoOriginal);
    fclose(archivoComprimido);
}
// comprimir un archivo usando rutas Huffman y guardar en un archivo general
unsigned int comprimir_archivo_y_guardar(FILE *salida, const char *ruta_archivo, char *codigos[256], unsigned char *bits_finales)
{
    FILE *entrada = fopen(ruta_archivo, "rb");
    if (!entrada)
    {
        perror("No se pudo abrir el archivo para comprimir");
        return 0;
    }

    char buffer_bits[MAX_RUTA] = "";
    int c;
    unsigned int bytes_escritos = 0;

    while ((c = fgetc(entrada)) != EOF)
    {
        strcat(buffer_bits, codigos[c]);

        while (strlen(buffer_bits) >= 8)
        {
            char byte_bin[9];
            strncpy(byte_bin, buffer_bits, 8);
            byte_bin[8] = '\0';

            unsigned char byte_escrito = (unsigned char)strtol(byte_bin, NULL, 2);
            fputc(byte_escrito, salida);
            bytes_escritos++;

            memmove(buffer_bits, buffer_bits + 8, strlen(buffer_bits) - 7);
        }
    }

    *bits_finales = strlen(buffer_bits);

    if (*bits_finales > 0)
    {
        while (strlen(buffer_bits) < 8)
        {
            strcat(buffer_bits, "0");
        }
        unsigned char byte_escrito = (unsigned char)strtol(buffer_bits, NULL, 2);
        fputc(byte_escrito, salida);
        bytes_escritos++;
    }

    fclose(entrada);
    return bytes_escritos;
}
//Función utilizada por los hijos para comprimir un archivo
//Comprime el archivo y le devuelve al padre la metadata
void comprimir_hijo(const char *ruta_archivo_entrada, int pipe_fd, int *frecuencias)
{
    // Leer las frecuencias

    // Reconstruir el arbol, dado que los hijos no comparten ram con el padre.
    NodoArbol **lista_arboles = NULL;
    int num_nodos = 0;
    crear_lista_arboles(frecuencias, &lista_arboles, &num_nodos);
    qsort(lista_arboles, num_nodos, sizeof(NodoArbol *), comparar_nodos);
    NodoArbol *raiz = contruir_arbol_de_huffman(lista_arboles, num_nodos);

    Ruta *rutas = NULL;
    int num_rutas = 0;
    rutasHojas(raiz, &rutas, &num_rutas);

    // 🆕 Paso 4: Crear tabla de códigos rápidos
    char *codigos[256] = {0};
    for (int i = 0; i < num_rutas; i++)
    {
        codigos[rutas[i].byte] = rutas[i].ruta;
    }

    // abrir el archivo a comprimir
    FILE *entrada = fopen(ruta_archivo_entrada, "rb");
    if (!entrada)
    {
        perror("Hijo: No se pudo abrir archivo de entrada");
        exit(1);
    }

    const char *nombre_archivo = strrchr(ruta_archivo_entrada, '/');
    nombre_archivo = nombre_archivo ? nombre_archivo + 1 : ruta_archivo_entrada;

    // Crear el temporar de salida
    char ruta_salida[512];
    snprintf(ruta_salida, sizeof(ruta_salida), "%s.huff.temp", nombre_archivo);
    FILE *salida = fopen(ruta_salida, "wb");
    if (!salida)
    {
        perror("Hijo: No se pudo crear archivo de salida");
        fclose(entrada);
        exit(1);
    }

    unsigned char byte_actual = 0;
    int bits_en_byte = 0;
    unsigned int bytes_escritos = 0;
    // recorro todos los bytes
    for (int c; (c = fgetc(entrada)) != EOF;)
    {
        // busco el byte en los codigos
        char *codigo = codigos[c];
        // recorro cada codigo hasta el final
        for (int i = 0; codigo[i] != '\0'; i++)
        {
            // Muevo el byte actual 1 espacio a la izquierda, 101-> 1010, y le hago un or con el codigo convertido a string
            // Seria esencialmente multiplicarlo por 2 y sumarle el numero
            // byte_actual = byte_actual * 2 + codigo[i] -'0'
            // Se le resta a codigo[i] el '0', para convertir el caracter a su representacion numerica
            // EN ascii el 1 es equivalente a 49, y el 0 a 48, osea 49- 48 =1
            byte_actual = (byte_actual << 1) | (codigo[i] - '0');
            bits_en_byte++;

            // si ya llene el byte lo escribo
            if (bits_en_byte == 8)
            {
                fputc(byte_actual, salida);
                bytes_escritos++;
                byte_actual = 0;
                bits_en_byte = 0;
            }
        }
    }

    unsigned char bits_finales = bits_en_byte;
    if (bits_en_byte > 0)
    {
        byte_actual <<= (8 - bits_en_byte); // lo muevo los espacios necesario para que sea un byte. Ejemplo 101->10100000
        fputc(byte_actual, salida);
        bytes_escritos++;
    }

    fclose(entrada);
    fclose(salida);

    // Enviar la salida
    MetaArchivo meta;
    strncpy(meta.nombre, nombre_archivo, sizeof(meta.nombre));
    meta.nombre[sizeof(meta.nombre) - 1] = '\0'; // ponerle fin
    meta.tam_comprimido = bytes_escritos;
    meta.bits_finales = bits_finales;

    // Enviar el struct de meta
    write(pipe_fd, &meta, sizeof(MetaArchivo));
    close(pipe_fd);

    exit(0);
}
//Cuenta las apariciones de un byte en un archivo 
void contar_frecuencias(FILE *file, int frecuencias[256])
{

    // Limpiar la memoria de la lista
    for (int i = 0; i < 256; i++)
    {
        frecuencias[i] = 0;
    }

    int byte;
    while ((byte = fgetc(file)) != EOF)
    {
        frecuencias[byte]++;
    }
}
//Cuenta las apariciones de un byte en todos los archivos de un directorio
void contar_frecuencias_en_directorio(const char *ruta_directorio, int *frecuencias)
{
    // Inicializar frecuencias globales

    printf("%s directorio \n", ruta_directorio);
    DIR *dir = opendir(ruta_directorio);
    if (!dir)
    {
        perror("No se pudo abrir el directorio");
        exit(1);
    }

    int fd[2];
    if (pipe(fd) == -1)
    {
        perror("Error al crear pipe de lectura de frecuencias");
        exit(1);
    }

    int hijos = 0;
    struct dirent *entrada;
    while ((entrada = readdir(dir)) != NULL)
    {
        // ignorar foldes internos y salidas a otros folders
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;

        pid_t pid = fork();
        if (pid == 0)
        {
            // Los hijos cuentan las frecuenccias de 1 archivo
            char ruta_completa[512];
            snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta_directorio, entrada->d_name);
            // obtener la ruta del archivo

            FILE *archivo = fopen(ruta_completa, "rb");
            if (!archivo)
            {
                perror("No se pudo abrir el archivo del directorio");
                continue;
            }

            // Contar las freq de ese archivo
            int freq_temp[256] = {0};

            contar_frecuencias(archivo, freq_temp);

            fclose(archivo);
            write(fd[1], freq_temp, sizeof(freq_temp));
            close(fd[1]);
            exit(0);
        }
        hijos++;
    }

    closedir(dir);
    close(fd[1]); // Cierro escritura

    // Reinicio las frecuencias a 0
    for (int i = 0; i < 256; i++)
    {
        frecuencias[i] = 0;
    }

    int frecuencias_locales[256];

    for (int i = 0; i < hijos; i++)
    {
        ssize_t bytes_leidos = read(fd[0], frecuencias_locales, sizeof(frecuencias_locales));
        if (bytes_leidos != sizeof(frecuencias_locales))
        {
            perror("Error al leer frecuencias de hijo");
            exit(1);
        }

        // sumar a las globales
        for (int j = 0; j < 256; j++)
        {
            frecuencias[j] += frecuencias_locales[j];
        }
    }

    close(fd[0]);

    while (wait(NULL) > 0)
        ;
}

//Une todos los archivos que terminen en .huff, que se encuentren en el arreglo de metadatas
void unir_huffs(char archivo_huff[MAX_RUTA], MetaArchivo *metadatos, int cantidad_archivos)
{
    FILE *archivo_salida = fopen(archivo_huff, "wb");
    if (!archivo_salida)
    {
        perror("No se pudo crear el archivo.huff general.");
        exit(1);
    }
    // pegar archivos
    for (int i = 0; i < cantidad_archivos; i++)
    {
        char nombre_temp[512];
        snprintf(nombre_temp, sizeof(nombre_temp), "%s.huff.temp", metadatos[i].nombre);

        FILE *entrada = fopen(nombre_temp, "rb");
        if (!entrada)
        {
            fprintf(stderr, "No se pudo abrir %s\n", nombre_temp);
            fclose(archivo_salida);
            exit(1);
        }

        int byte;
        while ((byte = fgetc(entrada)) != EOF)
        {
            fputc(byte, archivo_salida);
        }

        fclose(entrada);

        // ELiminar el temp
        if (remove(nombre_temp) != 0)
        {
            perror("Error al eliminar archivo temporal");
        }
    }

    fclose(archivo_salida);

    
}

//Dado un archivo.huff, anexa la metadata de los demas archivos a este
void annadir_metadata_al_huff(const char *nombre_archivo, Ruta *rutas, int num_rutas, MetaArchivo *metadatos, int total_archivos)
{
    FILE *f = fopen(nombre_archivo, "ab"); // Abrir en modo append binario
    if (!f)
    {
        perror("No se pudo abrir el archivo para escribir tabla y metadata");
        return;
    }

    // Ftell nos da la posicion actual en el archivo
    // La escribiremos al final del archivo para saber donde inicia la metadata
    long offset_tabla = ftell(f);

    // Escribir los codigos
    for (int i = 0; i < num_rutas; i++)
    {
        fprintf(f, "%s,%d\n", rutas[i].ruta, rutas[i].byte);
    }

    // Escribir el separador de codigos y meta
    fprintf(f, "---\n");

    // Escribir  la metadata
    for (int i = 0; i < total_archivos; i++)
    {
        fprintf(f, "%s,%u,%u\n", metadatos[i].nombre, metadatos[i].tam_comprimido, metadatos[i].bits_finales);
    }

    // Escribimos el offset al final.
    fwrite(&offset_tabla, sizeof(long), 1, f);

    fclose(f);
}
void compress_directory_con_fork(const char *ruta_directorio)
{
    int *frecuencias = mmap(NULL, sizeof(int) * 256,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS,
                            -1, 0);
    // memoria compartida, anonymous las inicializa en 0
    if (frecuencias == MAP_FAILED)
    {
        perror("Fallo en mmap, para el conteo de frecuencias");
        exit(1);
    }
    contar_frecuencias_en_directorio(ruta_directorio, frecuencias);

    // Se cuentan las frecuencias y se guardan en un archivo.temp

    // Saber cuantos archivos hay en el directorio
    

    struct dirent **archivos = NULL;
    int cantidad = scandir(ruta_directorio, &archivos, NULL, alphasort);
    if (cantidad < 0)
    {
        perror("Error al escanear el directorio");
        exit(1);
    }

    // Pipe para que los hijos envien metadata
    int fd[2];
    if (pipe(fd) == -1)
    {
        perror("Error al crear pipe");
        exit(1);
    }

    int hijos = 0;
    for (int i = 0; i < cantidad; i++)
    {
        const char *nombre = archivos[i]->d_name;
        if (strcmp(nombre, ".") == 0 || strcmp(nombre, "..") == 0)
            continue;

        pid_t pid = fork();
        if (pid == 0)
        {

            close(fd[0]); // Cerrar lectura
            char ruta_completa[512];
            snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta_directorio, nombre);
            comprimir_hijo(ruta_completa, fd[1], frecuencias);
        }

        hijos++;
        free(archivos[i]);
    }

    // esperar a que terminen los hijos
    while (wait(NULL) > 0)
        ;
    MetaArchivo *metadatos = calloc(hijos, sizeof(MetaArchivo));
    int leidos = 0;
    while (leidos < hijos)
    {
        MetaArchivo meta;
        ssize_t r = read(fd[0], &meta, sizeof(MetaArchivo));
        if (r == sizeof(MetaArchivo))
        {
            metadatos[leidos++] = meta;
        }
        else if (r == 0)
        {
            break; // EOF
        }
        else
        {
            perror("Error al leer del pipe");
            break;
        }
    }
    close(fd[0]);
    // Ordenar los metadatos por orden alfabecio
    qsort(metadatos, leidos, sizeof(MetaArchivo), comparar_meta_por_nombre);

    // reconstruir tabla de códigos y escribirla
    NodoArbol **lista_de_arboles = NULL;
    int num_nodos = 0;
    crear_lista_arboles(frecuencias, &lista_de_arboles, &num_nodos);
    qsort(lista_de_arboles, num_nodos, sizeof(NodoArbol *), comparar_nodos);
    NodoArbol *raiz_huffman = contruir_arbol_de_huffman(lista_de_arboles, num_nodos);

    Ruta *rutas = NULL;
    int num_rutas = 0;
    rutasHojas(raiz_huffman, &rutas, &num_rutas);

    // Escribir los metadatos.

    char copia_ruta[MAX_RUTA];
    strncpy(copia_ruta, ruta_directorio, sizeof(copia_ruta) - 1);
    copia_ruta[sizeof(copia_ruta) - 1] = '\0';
    const char *nombre_directorio = strrchr(copia_ruta, '/'); // Obtener el ultimo / si existe
    if (nombre_directorio)
    {
        nombre_directorio++; // Eliminar el /
    }
    else
    {
        nombre_directorio = ruta_directorio;
    }
    char nombre_archivo_huff[MAX_RUTA];
    snprintf(nombre_archivo_huff, sizeof(nombre_archivo_huff), "%s.huff", nombre_directorio);

    unir_huffs(nombre_archivo_huff, metadatos, hijos);
    annadir_metadata_al_huff(nombre_archivo_huff, rutas, num_rutas, metadatos, leidos);
    munmap(frecuencias, sizeof(int) * 256); // Liberar las frecuencias
    free(metadatos);
}

int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        fprintf(stderr, "Error: Falta el argumento de directorio\nUso: %s <directorio>\n", argv[0]);
        return 1;
    }

    if (argc > 2)
    {
        fprintf(stderr, "Error: Demasiados argumentos\nUso: %s <directorio>\n", argv[0]);
        return 1;
    }
    const char *ruta_directorio = argv[1];
    printf("\nCompresor con fork: \n");

    // lIMPIAR LA RUTA
    // Se quita el / final para que tanto books como books/ sirvan como argumento
    char ruta_directorio_limpia[PATH_MAX];
    strncpy(ruta_directorio_limpia, argv[1], sizeof(ruta_directorio_limpia) - 1);
    ruta_directorio_limpia[sizeof(ruta_directorio_limpia) - 1] = '\0';
    size_t len = strlen(ruta_directorio_limpia);
    if (len > 0 && ruta_directorio_limpia[len - 1] == '/')
    {
        ruta_directorio_limpia[len - 1] = '\0';
    }
    DIR *directorio = opendir(ruta_directorio_limpia);
    if (!directorio)
    {
        perror("Error abriendo el directorio");
        exit(1);
    }
    // Ver si el directorio está vacío
    int vacio = 1;

    struct dirent *elemento; // Elementos en el directorio
    while ((elemento = readdir(directorio)) != NULL)
    {
        if (strcmp(elemento->d_name, ".") != 0 && strcmp(elemento->d_name, "..") != 0) // Ignora subcarpetas y directorios padre
        {
            vacio = 0; // Encontró al menos un archivo
            break;
        }
    }

    closedir(directorio);

    if (vacio)
    {
        fprintf(stderr, "El directorio no tiene archivos. %s\n", ruta_directorio);
        exit(1);
    }

    struct timespec inicio, fin;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    compress_directory_con_fork(ruta_directorio_limpia);

    clock_gettime(CLOCK_MONOTONIC, &fin);

    // Calcular la diferencia en nanosegundos
    long segundos = fin.tv_sec - inicio.tv_sec;
    long nanosegundos = fin.tv_nsec - inicio.tv_nsec;
    long long tiempo_total_ns = segundos * 1000000000LL + nanosegundos;
    double tiempo_total_ms = tiempo_total_ns / 1e6;

    printf("Tiempo tardado: %lld nanosegundos (%.3f milisegundos)\n", tiempo_total_ns, tiempo_total_ms);

    return 0;
}
