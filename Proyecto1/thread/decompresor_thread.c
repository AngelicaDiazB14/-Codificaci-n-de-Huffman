#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
// #include <linux/limits.h> // Para PATH_MAX
#include <limits.h>
#include <unistd.h> // Para sysconf
#include "funciones_arboles.h"


#define TAM_BUFFER 65536          // Define un tamaño de buffer de 64 KB para leer los archivos comprimidos

typedef struct {
    char nombre[256];             // Nombre del archivo original (hasta 255 caracteres)
    unsigned int tam_comprimido;  // Tamaño del archivo comprimido
    unsigned char bits_finales;   // Número de bits restantes en el último byte del archivo comprimido
} MetaArchivo;                    // Estructura que almacena los metadatos de un archivo comprimido

typedef struct {
    const char *archivo_huff;         // Puntero al nombre del archivo comprimido en formato Huffman
    NodoArbolDecompresion *arbol;     // Puntero al árbol de Huffman usado para la descompresión
    MetaArchivo meta;                 // Información del archivo comprimido (metadatos)
    char directorio_salida[PATH_MAX]; // Directorio donde se guardará el archivo descomprimido
    long offset;                      // Desplazamiento en el archivo comprimido para comenzar la descompresión
} DatosHilo;                          // Estructura que contiene los datos necesarios para un hilo de descompresión


// =========================================================================
// Nombre de la función: descomprimir_archivo
// Encargada de: Descomprimir un archivo utilizando el algoritmo Huffman
// Entradas: Un puntero void que es convertido a un puntero a DatosHilo
// Salida: No tiene salida, se utiliza pthread_exit() para terminar el hilo.
// =========================================================================
void* descomprimir_archivo(void *arg) {
    // Convertir el argumento recibido a un puntero de tipo DatosHilo
    DatosHilo *datos = (DatosHilo*)arg;
    
    // Crear la ruta de salida concatenando el directorio de salida y el nombre del archivo
    char ruta_salida[PATH_MAX];
    if (snprintf(ruta_salida, sizeof(ruta_salida), "%s/%s", datos->directorio_salida, datos->meta.nombre) >= sizeof(ruta_salida)) {
        // Si el nombre del archivo excede el tamaño del buffer, se muestra un error y se termina el hilo.
        fprintf(stderr, "El nombre del archivo es demasiado largo para la ruta de salida.\n");
        pthread_exit(NULL);
    }

    // Intentar abrir el archivo comprimido para lectura binaria
    FILE *in = fopen(datos->archivo_huff, "rb");
    if (!in) {
        // Si no se puede abrir el archivo, se muestra un error y se termina el hilo
        perror("No se pudo abrir el archivo comprimido");
        pthread_exit(NULL);
    }
    
    // Intentar abrir el archivo de salida para escribir los datos descomprimidos
    FILE *out = fopen(ruta_salida, "wb");
    if (!out) {
        // Si no se puede abrir el archivo de salida, se muestra un error, se cierra el archivo comprimido y se termina el hilo
        perror("No se pudo abrir el archivo de salida");
        fclose(in);
        pthread_exit(NULL);
    }

    // Desplazar el puntero de archivo al lugar indicado por el offset
    fseek(in, datos->offset, SEEK_SET);

    unsigned char buffer[TAM_BUFFER]; // Buffer de tamaño 64KB para almacenar los datos leídos
    size_t bytes_restantes = datos->meta.tam_comprimido; // Total de bytes restantes por descomprimir
    NodoArbolDecompresion *actual = datos->arbol; // Nodo actual del árbol de descompresión
    int es_ultimo_bloque = 0; // Variable para determinar si estamos leyendo el último bloque de datos

    // Comenzar la descompresión mientras haya bytes restantes
    while (bytes_restantes > 0) {
        // Determinar el número de bytes a leer en este ciclo
        size_t leer = bytes_restantes > TAM_BUFFER ? TAM_BUFFER : bytes_restantes;
        size_t leidos = fread(buffer, 1, leer, in);
        if (leidos == 0) break; // Si no se han leído bytes, salir del ciclo

        // Determinar si el bloque que se lee es el último bloque
        es_ultimo_bloque = (bytes_restantes == leidos);
        bytes_restantes -= leidos; // Actualizar la cantidad de bytes restantes

        // Descomprimir byte por byte
        for (size_t i = 0; i < leidos; i++) {
            unsigned char byte = buffer[i];
            // Determinar cuántos bits usar en este byte, considerando si es el último bloque
            int bits = (es_ultimo_bloque && i == leidos - 1 && datos->meta.bits_finales > 0 && datos->meta.bits_finales < 8) ? 
                       datos->meta.bits_finales : 8;

            // Recorrer los bits del byte para encontrar el valor correspondiente en el árbol de descompresión
            for (int b = 7; b >= (8 - bits); b--) {
                actual = ((byte >> b) & 1) ? actual->der : actual->izq;
                // Si se ha llegado a un nodo con un byte válido, escribir el byte en el archivo de salida
                if (actual->byte != -1) {
                    fputc(actual->byte, out);
                    actual = datos->arbol; // Volver al nodo raíz del árbol
                }
            }
        }
    }

    // Cerrar los archivos de entrada y salida
    fclose(in);
    fclose(out);
    
    // Terminar el hilo
    pthread_exit(NULL);
}


// =========================================================================
// Nombre de la función: leer_meta_y_tabla
// Encargada de: Leer los metadatos y la tabla de codificación desde un archivo .meta
// Entradas: Un archivo .meta y punteros para almacenar los resultados
// Salida: 1 si todo fue leído correctamente, 0 en caso de error.
// =========================================================================
int leer_meta_y_tabla(const char *archivo_meta, Codigo **out_codigos, int *num_codigos, MetaArchivo **out_archivos, int *num_archivos) {
    // Abrir el archivo .meta en modo binario
    FILE *f = fopen(archivo_meta, "rb");
    if (!f) {
        perror("No se pudo abrir el archivo .meta");
        return 0;
    }

    // Ir al final del archivo para obtener el tamaño total
    fseek(f, 0, SEEK_END);
    long tam_total = ftell(f);

    // Desplazar el puntero al lugar donde se encuentra el offset de la tabla
    fseek(f, -sizeof(long), SEEK_END);
    long offset_tabla;
    
    // Leer el offset de la tabla desde el final del archivo
    size_t leidos = fread(&offset_tabla, sizeof(long), 1, f);
    if (leidos != 1) {
        perror("Error al leer el offset_tabla");
        fclose(f);
        return 0;
    }
    
    // Ir a la posición donde empieza la tabla de codificación
    fseek(f, offset_tabla, SEEK_SET);
    
    char linea[MAX_LINEA]; // Buffer para leer las líneas del archivo
    int leyendo_codigos = 1; // Indicador de si estamos leyendo la tabla de códigos

    Codigo *codigos = NULL; // Puntero para almacenar los códigos
    int total_codigos = 0;

    MetaArchivo *archivos = NULL; // Puntero para almacenar la lista de archivos
    int total_archivos = 0;
    
    // Leer el archivo línea por línea
    while (fgets(linea, MAX_LINEA, f)) {
        // Cuando se encuentra una línea con "---", se cambia a la lectura de archivos
        if (strcmp(linea, "---\n") == 0) {
            leyendo_codigos = 0;
            continue;
        }

        if (leyendo_codigos) {
            // Si estamos leyendo los códigos, procesarlos y almacenarlos
            codigos = realloc(codigos, (total_codigos + 1) * sizeof(Codigo));
            char *token = strtok(linea, ",");
            strncpy(codigos[total_codigos].ruta, token, sizeof(codigos[total_codigos].ruta) - 1);
            codigos[total_codigos].ruta[sizeof(codigos[total_codigos].ruta) - 1] = '\0'; // Asegurarse de que la cadena esté bien terminada
            token = strtok(NULL, "\n");
            codigos[total_codigos].byte = atoi(token);
            total_codigos++;
        } else {
            // Si estamos leyendo archivos, procesarlos y almacenarlos
            archivos = realloc(archivos, (total_archivos + 1) * sizeof(MetaArchivo));
            char *token = strtok(linea, ",");
            strncpy(archivos[total_archivos].nombre, token, sizeof(archivos[total_archivos].nombre) - 1);
            archivos[total_archivos].nombre[sizeof(archivos[total_archivos].nombre) - 1] = '\0'; // Asegurarse de que la cadena esté bien terminada
            token = strtok(NULL, ",");
            archivos[total_archivos].tam_comprimido = atoi(token);
            token = strtok(NULL, "\n");
            archivos[total_archivos].bits_finales = atoi(token);
            total_archivos++;
        }

        // Si hemos llegado al final del archivo, salir del ciclo
        if (ftell(f) >= tam_total - sizeof(long)) {
            break;
        }
    }
    
    // Cerrar el archivo .meta
    fclose(f);

    // Asignar los resultados a los punteros de salida
    *out_codigos = codigos;
    *num_codigos = total_codigos;
    *out_archivos = archivos;
    *num_archivos = total_archivos;
    
    return 1;
}


// =========================================================================
// Nombre de la función: obtener_numero_hilos_max
// Encargada de: Obtener el número máximo de hilos posibles según los núcleos de la CPU
// Entradas: Ninguna
// Salida: El número máximo de hilos disponibles según la cantidad de núcleos del sistema
// =========================================================================
int obtener_numero_hilos_max() {
    // Obtener el número de núcleos disponibles en el sistema
    int max_hilos = sysconf(_SC_NPROCESSORS_ONLN);
    return max_hilos; // Retornar el número de núcleos como número de hilos
}

// =========================================================================
// Nombre de la función: main
// Encargada de: Descomprimir archivos utilizando múltiples hilos para procesar los archivos comprimidos.
// Entradas: Argumentos de la línea de comandos (archivo .meta)
// Salida: Código de salida (1 si ocurre un error, 0 si termina correctamente)
// =========================================================================

int main(int argc, char *argv[]) {
    // Variables para medir el tiempo de ejecución
    struct timespec inicio, fin;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

   
    // Verificar que se haya pasado el número correcto de argumentos
    if (argc != 2) return 1;  // Si no se pasa el archivo .meta como argumento, termina el programa con código de error 1

    // Punteros a las estructuras donde se guardarán los datos de los códigos y archivos
    Codigo *codigos = NULL;
    MetaArchivo *archivos = NULL;
    int num_codigos = 0, num_archivos = 0;

    // Leer los datos del archivo .meta (estructura de codificación y archivos)
    if (!leer_meta_y_tabla(argv[1], &codigos, &num_codigos, &archivos, &num_archivos)) {
        return 1;  // Si la lectura falla, termina el programa con código de error 1
    }
    printf("\n Descompresor con hilos: \n");
    printf("\n Descompresión iniciada por favor espere... \n");

    // Reconstruir el árbol de Huffman a partir de los códigos leídos
    NodoArbolDecompresion *arbol = reconstruirArbol(codigos, num_codigos);
    free(codigos);  // Liberar la memoria de los códigos ya que ya no son necesarios

    // Preparar el nombre del directorio de salida, usando el nombre del archivo .meta sin la extensión
    char nombre_dir[256];
    strncpy(nombre_dir, argv[1], sizeof(nombre_dir) - 1);  // Copiar el nombre del archivo .meta
    char *punto = strrchr(nombre_dir, '.');  // Buscar la última aparición del punto en el nombre del archivo
    if (punto) *punto = '\0';  // Eliminar la extensión del archivo para usar el nombre base del archivo

    // Crear un directorio para los archivos descomprimidos
    char dir_salida[PATH_MAX];
    snprintf(dir_salida, sizeof(dir_salida), "%s_descompreso", nombre_dir);  // Crear un nombre para el directorio de salida
    mkdir(dir_salida, 0777);  // Crear el directorio con permisos 0777 (lectura, escritura y ejecución para todos)

    // Determinar el número máximo de hilos disponibles en el sistema
    int max_hilos = obtener_numero_hilos_max();  // Obtener el número de núcleos del procesador
    pthread_t hilos[max_hilos];  // Array de identificadores de hilos
    DatosHilo datos_hilos[max_hilos];  // Array de datos para los hilos (información de cada archivo)
    int hilos_activos = 0;  // Contador de hilos activos
    long offset = 0;  // Variable para almacenar el offset de cada archivo comprimido

    // Iterar sobre todos los archivos para descomprimirlos
    for (int i = 0; i < num_archivos; i++) {
        // Asignar los datos específicos para este hilo (archivo, árbol y meta-información)
        datos_hilos[hilos_activos] = (DatosHilo){
            .archivo_huff = argv[1],  // Archivo .meta con la información comprimida
            .arbol = arbol,  // Árbol de Huffman para la descompresión
            .meta = archivos[i],  // Información específica del archivo a descomprimir
            .offset = offset  // Offset del archivo dentro del archivo comprimido
        };
        
        // Asignar el directorio de salida al hilo
        strncpy(datos_hilos[hilos_activos].directorio_salida, dir_salida, PATH_MAX - 1);
        
        // Actualizar el offset para el siguiente archivo
        offset += archivos[i].tam_comprimido;

        // Crear un hilo para descomprimir el archivo
        pthread_create(&hilos[hilos_activos], NULL, descomprimir_archivo, &datos_hilos[hilos_activos]);
        hilos_activos++;  // Incrementar el contador de hilos activos

        // Esperar si hemos alcanzado el máximo número de hilos o si es el último archivo
        if (hilos_activos == max_hilos || i == num_archivos - 1) {
            // Esperar a que todos los hilos activos terminen su ejecución
            for (int j = 0; j < hilos_activos; j++) {
                pthread_join(hilos[j], NULL);  // Esperar que cada hilo termine
            }
            hilos_activos = 0;  // Resetear el contador de hilos activos para el siguiente bloque de hilos
        }
    }

    // Liberar la memoria utilizada para los archivos
    free(archivos);

   

    clock_gettime(CLOCK_MONOTONIC, &fin);

    // Calcular la diferencia en nanosegundos
    long segundos = fin.tv_sec - inicio.tv_sec;
    long nanosegundos = fin.tv_nsec - inicio.tv_nsec;
    long long tiempo_total_ns = segundos * 1000000000LL + nanosegundos;
    double tiempo_total_ms = tiempo_total_ns / 1e6;

    printf("Tiempo tardado: %lld nanosegundos (%.3f milisegundos)\n", tiempo_total_ns, tiempo_total_ms);

    return 0;  // Retornar 0 al finalizar correctamente
}
