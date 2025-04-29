#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // mkdir
#include <sys/types.h> // mode_t
#include <errno.h>
#include <sys/time.h>
#include <inttypes.h>
#include <time.h>
#include "funciones_arboles.h"
// #include <linux/limits.h>
#include <limits.h>

typedef struct
{
    char nombre[256];
    unsigned int tam_comprimido;
    unsigned char bits_finales;
} MetaArchivo;

// Procesar codigos
// Recibe un string con los codigos y la cantidad de codigos encontrados
// Devuelve los codigos en forma de arreglo
Codigo *procesarCodigos(char **codigos, int num_codigos)
{
    Codigo *codigoEstructura = calloc(num_codigos, sizeof(Codigo));
    if (!codigoEstructura)
    {
        perror("Error al asignar memoria para códigos");
        return NULL;
    }

    for (int i = 0; i < num_codigos; i++)
    {
        char *token = strtok(codigos[i], ",");
        if (!token)
        {
            fprintf(stderr, "Error al procesar la línea: %s\n", codigos[i]);
            continue;
        }

        strcpy(codigoEstructura[i].ruta, token);

        token = strtok(NULL, "\n"); // segunda parte codigo
        if (!token)
        {
            fprintf(stderr, "Error: no se encontró el byte en la línea\n");
            continue;
        }

        codigoEstructura[i].byte = atoi(token); // Pasar el strring a entero
    }

    return codigoEstructura;
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
        // printf("Línea leída: %s", linea);
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

void descomprimir_multiples(const char *archivo_huff)
{
    Codigo *codigos = NULL;
    int num_codigos = 0;
    MetaArchivo *archivos = NULL;
    int num_archivos = 0;

    if (!leer_meta_y_tabla(archivo_huff, &codigos, &num_codigos, &archivos, &num_archivos))
    {
        perror("Error al leer el meta");
        return;
    }

    // De los codigos obtenidos reconstruir el arbol de codigos.
    NodoArbolDecompresion *arbol = reconstruirArbol(codigos, num_codigos);

    FILE *f = fopen(archivo_huff, "rb");
    if (!f)
    {
        perror("No se pudo abrir el archivo .huff");
        return;
    }

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
        fclose(f);
        return;
    }

    for (int i = 0; i < num_archivos; i++)
    {
        MetaArchivo meta = archivos[i];

        // Obtenemos el nombre del archivo
        char ruta_salida[PATH_MAX];
        snprintf(ruta_salida, sizeof(ruta_salida), "%s/%s", directorio_salida, meta.nombre);

        FILE *out = fopen(ruta_salida, "wb");
        if (!out)
        {
            perror("No se pudo abrir archivo de salida");
            continue;
        }

        // Nos devolvemos a la raiz del arbol
        NodoArbolDecompresion *actual = arbol;

        for (long int bytes_leidos = 0; bytes_leidos < meta.tam_comprimido; bytes_leidos++)
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
            if (bytes_leidos == meta.tam_comprimido - 1 && meta.bits_finales > 0 && meta.bits_finales < 8)
            {
                bits_a_leer = meta.bits_finales; // Solo los bits útiles
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
    }

    free(codigos);
    free(archivos);
    fclose(f);
}

int main(int argc, char *argv[])
{

    
   
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

    printf("\nDecompresor Serial: \n");
    
    printf("\nDescompresión iniciada por favor espere... \n");

    const char *ruta_archivo_huff = argv[1];
    

    struct timespec inicio, fin;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    descomprimir_multiples(ruta_archivo_huff);

    clock_gettime(CLOCK_MONOTONIC, &fin);

    // Calcular la diferencia en nanosegundos
    long segundos = fin.tv_sec - inicio.tv_sec;
    long nanosegundos = fin.tv_nsec - inicio.tv_nsec;
    long long tiempo_total_ns = segundos * 1000000000LL + nanosegundos;
    double tiempo_total_ms = tiempo_total_ns / 1e6;

    printf("Tiempo tardado: %lld nanosegundos (%.3f milisegundos)\n", tiempo_total_ns, tiempo_total_ms);
    return 0;
}
