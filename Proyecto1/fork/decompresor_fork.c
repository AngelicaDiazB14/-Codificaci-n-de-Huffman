#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // mkdir
#include <sys/types.h> // mode_t
#include <errno.h>
#include <sys/time.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h> // Incluir fork. Solo funciona en linux
#include <sys/wait.h>
#include "funciones_arboles.h"
#include <limits.h>
// #include <linux/limits.h>
#define MAX_LINEA 256

typedef struct
{
    char nombre[256];
    unsigned int tam_comprimido;
    unsigned char bits_finales;
} MetaArchivo;

int leer_meta(const char *archivo_meta, MetaArchivo **out_archivos, int *num_archivos)
{
    FILE *f = fopen(archivo_meta, "r");
    if (!f)
    {
        perror("No se pudo abrir el archivo .meta");
        return 0;
    }

    char linea[MAX_LINEA];
    int leyendo_codigos = 1;

    MetaArchivo *archivos = NULL;
    int total_archivos = 0;

    while (fgets(linea, MAX_LINEA, f))
    {
        if (strcmp(linea, "---\n") == 0)
        {
            leyendo_codigos = 0;
            continue;
        }

        if (!leyendo_codigos)
        {
            archivos = realloc(archivos, (total_archivos + 1) * sizeof(MetaArchivo));
            char *token = strtok(linea, ",");
            strcpy(archivos[total_archivos].nombre, token);
            token = strtok(NULL, ",");
            archivos[total_archivos].tam_comprimido = atoi(token);
            token = strtok(NULL, "\n");
            archivos[total_archivos].bits_finales = atoi(token);
            total_archivos++;
        }
    }

    fclose(f);

    *out_archivos = archivos;
    *num_archivos = total_archivos;
    return 1;
}

int leer_meta_y_tabla(const char *archivo_meta, Codigo **out_codigos, int *num_codigos, MetaArchivo **out_archivos, int *num_archivos)
{
    FILE *f = fopen(archivo_meta, "rb");
    if (!f)
    {
        perror("No se pudo abrir el archivo .meta");
        return 0;
    }

    // Obtener el tamaño del archivo
    fseek(f, 0, SEEK_END);
    long tam_total = ftell(f);

    // Movernos al final del archivo y obtener el offset de donde empiezan los codigos

    fseek(f, -sizeof(long), SEEK_END);
    long offset_tabla;

    fread(&offset_tabla, sizeof(long), 1, f);

    // Movernos a dicho offset
    fseek(f, offset_tabla, SEEK_SET);

    char linea[MAX_LINEA];
    int leyendo_codigos = 1;

    Codigo *codigos = NULL;
    int total_codigos = 0;

    MetaArchivo *archivos = NULL;
    int total_archivos = 0;

    while (fgets(linea, MAX_LINEA, f))
    {
        // printf("📄 Línea leída: %s", linea);
        if (strcmp(linea, "---\n") == 0)
        {
            leyendo_codigos = 0;

            continue;
        }

        if (leyendo_codigos)
        {
            codigos = realloc(codigos, (total_codigos + 1) * sizeof(Codigo));
            char *token = strtok(linea, ",");
            strcpy(codigos[total_codigos].ruta, token);
            token = strtok(NULL, "\n");
            codigos[total_codigos].byte = atoi(token);
            total_codigos++;
        }
        else
        {
            archivos = realloc(archivos, (total_archivos + 1) * sizeof(MetaArchivo));
            char *token = strtok(linea, ",");
            strcpy(archivos[total_archivos].nombre, token);
            token = strtok(NULL, ",");
            archivos[total_archivos].tam_comprimido = atoi(token);
            token = strtok(NULL, "\n");
            archivos[total_archivos].bits_finales = atoi(token);
            total_archivos++;
        }

        // Si llegamos al final de los metas, dejamos de leer.
        if (ftell(f) >= tam_total - sizeof(long))
        {
            break;
        }
    }

    fclose(f);
    *out_codigos = codigos;
    *num_codigos = total_codigos;
    *out_archivos = archivos;
    *num_archivos = total_archivos;
    return 1;
}

int leer_solo_tabla(const char *archivo_meta, Codigo **out_codigos, int *num_codigos)
{
    FILE *f = fopen(archivo_meta, "rb");
    if (!f)
    {
        perror("No se pudo abrir el archivo .meta");
        return 0;
    }

    // Obtener el tamaño del archivo
    fseek(f, 0, SEEK_END);

    // Movernos al final del archivo y obtener el offset de donde empiezan los codigos

    fseek(f, -sizeof(long), SEEK_END);
    long offset_tabla;

    fread(&offset_tabla, sizeof(long), 1, f);

    // Movernos a dicho offset
    fseek(f, offset_tabla, SEEK_SET);

    char linea[MAX_LINEA];

    Codigo *codigos = NULL;
    int total_codigos = 0;

    while (fgets(linea, MAX_LINEA, f))
    {
        // printf("📄 Línea leída: %s", linea);
        // Si llego al separador termino de leer codigos.
        if (strcmp(linea, "---\n") == 0)
        {
            break;
        }

        codigos = realloc(codigos, (total_codigos + 1) * sizeof(Codigo));
        char *token = strtok(linea, ",");
        strcpy(codigos[total_codigos].ruta, token);
        token = strtok(NULL, "\n");
        codigos[total_codigos].byte = atoi(token);
        total_codigos++;
    }

    fclose(f);
    *out_codigos = codigos;
    *num_codigos = total_codigos;

    return 1;
}

void descomprimir_archivo_hijo(char directorio_salida[300], const char *archivo_huff, MetaArchivo *meta, long offset)
{
    // la tabla
    Codigo *codigos = NULL;
    int num_codigos = 0;

    if (!leer_solo_tabla(archivo_huff, &codigos, &num_codigos))
    {
        perror("Error al leer la tabla de códigos desde el .huff");
        return;
    }

    // construir el arbol
    NodoArbolDecompresion *arbol = reconstruirArbol(codigos, num_codigos);
    free(codigos);

    // Abrir el archivo huff grande
    FILE *f = fopen(archivo_huff, "rb");
    if (!f)
    {
        perror("Hijo: No se pudo abrir archivo .huff");
        return;
    }

    fseek(f, offset, SEEK_SET); // Buscar desde donde descomprimir

    // Crear la salida
    char ruta_salida[PATH_MAX];
    snprintf(ruta_salida, sizeof(ruta_salida), "%s/%s", directorio_salida, meta->nombre);

    FILE *out = fopen(ruta_salida, "wb");
    if (!out)
    {
        perror("Hijo: No se pudo crear archivo de salida");
        printf("Salida esperada %s \n", ruta_salida);
        return;
    }

    // Recorrer árbol de Huffman
    NodoArbolDecompresion *actual = arbol;
    for (long int bytes_leidos = 0; bytes_leidos < meta->tam_comprimido; bytes_leidos++)
    {
        // leer el siguiente byde
        int byte = fgetc(f);
        if (byte == EOF)
        {
            fprintf(stderr, "Error: se alcanzó EOF antes de leer todos los bytes\n");
            break;
        }

        int bits_a_leer = 8;
        // Si llegamos al ultimo byte y existen bits utiles
        if (bytes_leidos == meta->tam_comprimido - 1 && meta->bits_finales > 0 && meta->bits_finales < 8)
        {
            bits_a_leer = meta->bits_finales; // Solo los bits útiles
        }
        // leemos de izquierda a derecha el byte a escribir
        for (int b = 7; b >= (8 - bits_a_leer); b--)
        {
            // desplazo los bits y le hago un and para quedarme con el bit a desplazar
            int bit = (byte >> b) & 1;
            if (bit == 0)
            {
                // Si es 0 voy por la izquierda
                actual = actual->izq;
            }
            else
            {
                actual = actual->der;
            }

            // si encontramos un byte, lo escribimos
            if (actual->byte != -1)
            {
                fputc(actual->byte, out);
                actual = arbol;
            }
        }
    }

    fclose(out);
    // printf("Archivo restaurado: %s\n", ruta_salida);

    // printf("Se restauro %s\n", ruta_salida);
}

void descomprimir_multiples_con_fork(const char *archivo_huff)
{
    Codigo *codigos = NULL;
    int num_codigos = 0;
    MetaArchivo *archivos = NULL;
    int num_archivos = 0;

    // En la version paralela solo ocupamos el meta en un inicio
    if (!leer_meta_y_tabla(archivo_huff, &codigos, &num_codigos, &archivos, &num_archivos))
    {
        perror("Error al leer el meta");
        return;
    }

    /*
     printf("Archivos encontrados en %s:\n", archivo_meta);
     for (int i = 0; i < num_archivos; i++) {
         printf(" - %s (comprimido: %u bytes, bits útiles último byte: %u)\n",
                archivos[i].nombre,
                archivos[i].tam_comprimido,
                archivos[i].bits_finales);
     }*/

    // Calcular donde se empieza a descomprimir cada archivo
    long *offsets = calloc(num_archivos, sizeof(long));
    offsets[0] = 0;
    for (int i = 1; i < num_archivos; i++)
    {
        offsets[i] = offsets[i - 1] + archivos[i - 1].tam_comprimido;
    }

    // Crear la salida
    // Obtener el nombre del directorio
    char nombre_directorio[256];
    strncpy(nombre_directorio, archivo_huff, sizeof(nombre_directorio) - 1);
    nombre_directorio[sizeof(nombre_directorio) - 1] = '\0';
    char *punto = strrchr(nombre_directorio, '.');
    if (punto)
    {
        *punto = '\0'; // EN donde iría el .huff, reemplazar el . por un símbolo de fin
    }

    char directorio_salida[300];
    snprintf(directorio_salida, sizeof(directorio_salida), "%s_descompreso", nombre_directorio);

    if (mkdir(directorio_salida, 0777) && errno != EEXIST)
    {
        perror("No se pudo crear el directorio de salida");
        free(archivos);
        free(offsets);
        return;
    }

    // Delegar el trabajo a cada hijo
    for (int i = 0; i < num_archivos; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {

            descomprimir_archivo_hijo(directorio_salida, archivo_huff, &archivos[i], offsets[i]);
            exit(0);
        }
    }

    // 🧍 Esperar a todos los hijos
    while (wait(NULL) > 0)
        ;

    // 🧹 Limpieza
    free(archivos);
    free(offsets);
    free(codigos);
    // free(archivos);
}

int main(int argc, char *argv[])
{

    struct timeval start, end;
    gettimeofday(&start, NULL);
    if (argc < 2)
    {
        fprintf(stderr, "Error: Falta el argumento de archivo huff\nUso: %s <archivoHuff>\n", argv[0]);
        return 1;
    }
    if (argc > 2)
    {
        fprintf(stderr, "Error: Demasiados argumentos\nUso: %s <archivoHuff>\n", argv[0]);
        return 1;
    }
    printf("\nCompresor con fork: \n");

    printf("\nDescompresión iniciada por favor espere... \n");

    const char *ruta_archivo_huff = argv[1];
    descomprimir_multiples_con_fork(ruta_archivo_huff);

    gettimeofday(&end, NULL);
    long seconds = end.tv_sec - start.tv_sec;
    long microseconds = end.tv_usec - start.tv_usec;
    double time_taken = seconds * 1000.0 + microseconds / 1000.0; // en milisegundos

    printf("La ejecucion duro %f milisegundos\n", time_taken);
    return 0;

    // Cambiar mallocs a callocs. Cambiar [i].propiedad a arreglo->propiedad
    // Limpieza
    // refactorizacion
    // Crear .h .c .maker
}
