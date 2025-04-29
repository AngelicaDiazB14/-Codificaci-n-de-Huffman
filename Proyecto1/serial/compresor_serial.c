#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/time.h>
#include <inttypes.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include "funciones_arboles.h"
#include <limits.h>
// #include <linux/limits.h>

#define TAMANNO_BUFFER_BITS 100 // Tamaño en bits del buffer de escritura al comprimir
// El largo mas grande realmente seria 15 bits. Por si acaso se ponen mas

typedef struct
{
    char nombre[256];            // Nombre del archivo original
    unsigned int tam_comprimido; // Tamaño en bytes del archivo comprimido
    unsigned char bits_finales;  // Bits útiles en el último byte
} MetaArchivo;

// Función para comparar nodos, se compara que aquellos con mayor frecuencia vayan antes en el arreglo.
int comparar_nodos(const void *a, const void *b)
{
    NodoArbol *nodoA = *((NodoArbol **)a);
    NodoArbol *nodoB = *((NodoArbol **)b);

    return nodoA->frecuencia - nodoB->frecuencia;
}

// Ordenar los nombres de archivos alfabeticamente
// Sirve para que independientemente de el orden en que se lean los archivos, siempre se genera la misma metada que se genere en la version serial.
int comparar_meta_por_nombre(const void *a, const void *b)
{
    const MetaArchivo *ma = (const MetaArchivo *)a;
    const MetaArchivo *mb = (const MetaArchivo *)b;
    return strcmp(ma->nombre, mb->nombre);
}

// Cuenta cuantas veces aparece un byte en un archivo

void contar_frecuencias(FILE *file, int frecuencias[256])
{

    // Limpiar la memoria de la lista
    for (int i = 0; i < 256; i++)
    {
        frecuencias[i] = 0;
    }

    int byte;
    // Obtiene el byte siguiente, mientras no termine
    // Dado que cada byte tiene 8bits, cada byte representa un número de 0 a 255.
    while ((byte = fgetc(file)) != EOF)
    {
        frecuencias[byte]++;
    }
}
// Por cada archivo, cuenta las frecuencias de cada byte.
void contar_frecuencias_en_directorio(const char *ruta_directorio, int frecuencias[256])
{
    // Inicializar frecuencias globales
    for (int i = 0; i < 256; i++)
    {
        frecuencias[i] = 0;
    }
    printf("%s directorio \n", ruta_directorio);
    DIR *dir = opendir(ruta_directorio);
    if (!dir)
    {
        perror("No se pudo abrir el directorio");
        exit(1);
    }

    struct dirent *entrada;
    while ((entrada = readdir(dir)) != NULL)
    {
        // ignorar foldes internos y salidas a otros folders
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;

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
        // Se crea un arreglo temporal, para este archivo.
        int freq_temp[256] = {0};
        contar_frecuencias(archivo, freq_temp);
        for (int i = 0; i < 256; i++)
        {
            // Suma las frecuencias del archivo a las frecuencias globales.
            frecuencias[i] += freq_temp[i];
        }

        fclose(archivo);
    }

    closedir(dir);
}

// comprimir un archivo usando rutas Huffman y anexar al archivo de salida.
// El archivo de salido o está vacio, o se le anexa el resto de archivos.
unsigned int comprimir_archivo_y_guardar(FILE *salida, const char *ruta_archivo, char *codigos[256], unsigned char *bits_finales)
{
    FILE *entrada = fopen(ruta_archivo, "rb");
    if (!entrada)
    {
        fprintf(stderr, " No se pudo abrir el archivo a comprimir. '%s': %s\n", ruta_archivo, strerror(errno));

        return 1;
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

    *bits_finales = bits_en_byte;

    if (bits_en_byte > 0)
    {
        byte_actual <<= (8 - bits_en_byte); // lo muevo los espacios necesario para que sea un byte. Ejemplo 101->10100000
        fputc(byte_actual, salida);
        bytes_escritos++;
    }

    fclose(entrada);
    return bytes_escritos;
}

// Fncion que escribe despues de el archivo .huff.
// La funcion guarda la tabla de codigos, y las metadadas, ademas al final un offset de donde empieza la metadata.
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

void comprimir_directorio(const char *ruta_directorio)
{
    int frecuencias[256];
    contar_frecuencias_en_directorio(ruta_directorio, frecuencias);

    // Construir árbol de Huffman
    NodoArbol **lista_de_arboles = NULL;
    int num_nodos = 0;
    crear_lista_arboles(frecuencias, &lista_de_arboles, &num_nodos);
    qsort(lista_de_arboles, num_nodos, sizeof(NodoArbol *), comparar_nodos);
    NodoArbol *raiz_huffman = contruir_arbol_de_huffman(lista_de_arboles, num_nodos);

    // Obtener rutas (códigos Huffman)
    Ruta *rutas = NULL;
    int num_rutas = 0;
    rutasHojas(raiz_huffman, &rutas, &num_rutas);

    // LIMPIAR EL ARBOL
    limpiar_arbol(raiz_huffman);
    free(lista_de_arboles);

    // Generar tabla de códigos rápida
    char *codigos[256] = {0};
    for (int i = 0; i < num_rutas; i++)
    {
        codigos[rutas[i].byte] = rutas[i].ruta;
    }
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
    char nombre_archivo_meta[MAX_RUTA];
    snprintf(nombre_archivo_meta, sizeof(nombre_archivo_meta), "%s.meta", nombre_directorio);
    char nombre_archivo_huff[MAX_RUTA];
    snprintf(nombre_archivo_huff, sizeof(nombre_archivo_huff), "%s.huff", nombre_directorio);
    // Abrir archivo de salida
    FILE *archivo_comprimido = fopen(nombre_archivo_huff, "wb");
    if (!archivo_comprimido)
    {
        perror("No se pudo crear el archivo para compresión.");
        return;
    }

    // Lista de metadatos por archivo
    MetaArchivo *metadatos = NULL;
    int total_archivos = 0;

    // Recorrer archivos del directorio
    DIR *dir = opendir(ruta_directorio);
    if (!dir)
    {
        perror("No se pudo abrir el directorio.");
        return;
    }

    struct dirent *entrada;
    while ((entrada = readdir(dir)) != NULL)
    {
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;
        // Obtener la ruta completa de cad archivo
        char ruta_completa[512];
        snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta_directorio, entrada->d_name);

        unsigned char bits_finales = 0;
        unsigned int tam_comprimido = comprimir_archivo_y_guardar(archivo_comprimido, ruta_completa, codigos, &bits_finales);

        // Guardar metadata
        // Annadir un metadata a la lista de archivos.
        metadatos = realloc(metadatos, sizeof(MetaArchivo) * (total_archivos + 1));
        strcpy(metadatos[total_archivos].nombre, entrada->d_name);
        metadatos[total_archivos].tam_comprimido = tam_comprimido;
        metadatos[total_archivos].bits_finales = bits_finales;
        total_archivos++;
    }

    fclose(archivo_comprimido);
    closedir(dir);

    annadir_metadata_al_huff(nombre_archivo_huff, rutas, num_rutas, metadatos, total_archivos);

    
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
    printf("\nCompresor Serial: \n");

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

    comprimir_directorio(ruta_directorio_limpia);

    clock_gettime(CLOCK_MONOTONIC, &fin);

    
    long segundos = fin.tv_sec - inicio.tv_sec;
    long nanosegundos = fin.tv_nsec - inicio.tv_nsec;
    long long tiempo_total_ns = segundos * 1000000000LL + nanosegundos;
    double tiempo_total_ms = tiempo_total_ns / 1e6;

    printf("Tiempo tardado: %lld nanosegundos (%.3f milisegundos)\n", tiempo_total_ns, tiempo_total_ms);

    return 0;
}
