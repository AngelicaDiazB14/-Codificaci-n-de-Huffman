
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
#include <pthread.h>  // biblioteca para el manejo de hilos

#define TAMANNO_BUFFER_BITS 100 //Tamaño en bits del buffer de escritura al comprimir
//El largo mas grande realmente seria 15 bits. Por si acaso se ponen mas

// Mutex para proteger la escritura de metadatos y el acceso al archivo comprimido
pthread_mutex_t archivo_mutex;   // Mutex para sincronizar acceso al archivo
pthread_mutex_t metadatos_mutex; // Mutex para sincronizar acceso a metadatos

// Estructura para almacenar información sobre archivos comprimidos
typedef struct
{
    char nombre[256];            // Nombre del archivo original
    unsigned int tam_comprimido; // Tamaño en bytes del archivo comprimido
    unsigned char bits_finales;  // Bits útiles en el último byte
} MetaArchivo;

// Estructura para pasar datos a los hilos
typedef struct {
    char ruta_completa[512];       // Ruta del archivo a comprimir
    char *codigos[256];            // Tabla de códigos Huffman
    FILE *archivo_comprimido;      // Archivo donde se escribe la salida
    MetaArchivo *metadato;         // Metadato para este archivo
} DatosHilo;

// =========================================================================
// Nombre de la función: comparar_nodos
// Encargada de: Comparar la frecuencia de dos nodos del árbol Huffman
// Entradas: Dos punteros void que serán convertidos a NodoArbol**
// Salida: Diferencia entre las frecuencias de los nodos
// =========================================================================
int comparar_nodos(const void *a, const void *b)
{
    NodoArbol *nodoA = *((NodoArbol **)a);
    NodoArbol *nodoB = *((NodoArbol **)b);
    return nodoA->frecuencia - nodoB->frecuencia;
}

// =========================================================================
// Nombre de la función: comparar_meta_por_nombre
// Encargada de: Comparar nombres de archivos para ordenarlos alfabéticamente
// Entradas: Dos punteros void que serán convertidos a MetaArchivo*
// Salida: Un número que indica si el primer nombre va antes, es igual o va 
// después del segundo (orden de diccionario)
// =========================================================================
int comparar_meta_por_nombre(const void *a, const void *b) {
    const MetaArchivo *ma = (const MetaArchivo *)a;
    const MetaArchivo *mb = (const MetaArchivo *)b;
    return strcmp(ma->nombre, mb->nombre);
}


// =========================================================================
// Nombre de la función: contar_frecuencias
// Encargada de: Contar ocurrencias de cada byte en un archivo
// Entradas: Puntero a archivo y array para almacenar frecuencias
// Salida: void, modifica el array de frecuencias
// =========================================================================
void contar_frecuencias(FILE *file, int frecuencias[256])
{
    // Inicializar array de frecuencias a 0
    for (int i = 0; i < 256; i++)
    {
        frecuencias[i] = 0;
    }

    int byte;
    // Leer byte a byte hasta EOF incrementando contadores
    while ((byte = fgetc(file)) != EOF)
    {
        frecuencias[byte]++;
    }
}

// =========================================================================
// Nombre de la función: contar_frecuencias_en_directorio
// Encargada de: Calcular frecuencias totales de bytes en todos los archivos
// Entradas: Ruta del directorio y array para frecuencias
// Salida: void, modifica el array de frecuencias
// =========================================================================
void contar_frecuencias_en_directorio(const char *ruta_directorio, int frecuencias[256])
{
    // Inicializar el array de frecuencias a 0
    for (int i = 0; i < 256; i++)
    {
        frecuencias[i] = 0;
    }

    // Imprimir el nombre del directorio
    printf("%s directorio \n", ruta_directorio);
    
    // Abrir el directorio especificado
    DIR *dir = opendir(ruta_directorio);
    if (!dir)
    {
    // Si no se pudo abrir, mostrar error y terminar el programa
        perror("No se pudo abrir el directorio");
        exit(1);
    }

    struct dirent *entrada;
    // Leer cada entrada (archivo o carpeta) dentro del directorio
    while ((entrada = readdir(dir)) != NULL)
    {
    // Ignorar las entradas "." y ".."
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;

    // Construir la ruta completa hacia el archivo actual
        char ruta_completa[512];
        snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta_directorio, entrada->d_name);

    // Abrir el archivo en modo binario de solo lectura
        FILE *archivo = fopen(ruta_completa, "rb");
        if (!archivo)
        {
    // Si no se puede abrir el archivo, mostrar error y continuar con el siguiente
            perror("No se pudo abrir el archivo del directorio");
            continue;
        }

    // Crear un array temporal para contar frecuencias del archivo actual
        int freq_temp[256] = {0};
        // Contar las frecuencias de bytes en el archivo
        contar_frecuencias(archivo, freq_temp);
        
    // Sumar las frecuencias del archivo actual al acumulado general
        for (int i = 0; i < 256; i++)
        {
            frecuencias[i] += freq_temp[i];
        }

    // Cerrar el archivo
        fclose(archivo);
    }

    // Cerrar el directorio
    closedir(dir);
}


// =========================================================================
// Nombre de la función: comprimir_archivo_y_guardar
// Encargada de: Comprimir un archivo usando codificación Huffman
// Entradas: Archivo salida, ruta archivo entrada, códigos Huffman, puntero bits finales
// Salida: Número de bytes escritos
// =========================================================================

unsigned int comprimir_archivo_y_guardar(FILE *salida, const char *ruta_archivo, char *codigos[256], unsigned char *bits_finales)
{
    // Abrir el archivo de entrada en modo binario
    FILE *entrada = fopen(ruta_archivo, "rb");
    if (!entrada)
    {
        // Si no se puede abrir, mostrar error y salir
        fprintf(stderr, " No se pudo abrir el archivo a comprimir. '%s': %s\n", ruta_archivo, strerror(errno));
        return 1;
    }

    // Inicializar buffer temporal para almacenar datos comprimidos
    unsigned char *buffer_temp = NULL;
    size_t buffer_size = 0;          // Tamaño actual de datos en el buffer
    size_t buffer_capacity = 0;      // Capacidad total del buffer
    
    // Variables para construcción de bytes comprimidos
    unsigned char byte_actual = 0;   // Byte que se va llenando bit a bit
    int bits_en_byte = 0;            // Número de bits escritos en el byte_actual
    unsigned int bytes_escritos = 0; // Contador de bytes escritos en total
    
    // Leer cada byte del archivo de entrada
    for (int c; (c = fgetc(entrada)) != EOF;) {
        // Obtener el código Huffman correspondiente al byte leído
        char *codigo = codigos[c];
        
        // Procesar cada bit del código Huffman
        for (int i = 0; codigo[i] != '\0'; i++) {
            //Muevo el byte actual 1 espacio a la izquierda, 101-> 1010, y le hago un or con el codigo convertido a string
            //Seria esencialmente multiplicarlo por 2 y sumarle el numero
            //byte_actual = byte_actual * 2 + codigo[i] -'0'
            //Se le resta a codigo[i] el '0', para convertir el caracter a su representacion numerica
            //EN ascii el 1 es equivalente a 49, y el 0 a 48, osea 49- 48 =1
            byte_actual = (byte_actual << 1) | (codigo[i] - '0');
            bits_en_byte++;
            
            // Cuando se completa un byte (8 bits)
            if (bits_en_byte == 8) {
                // Aumentar tamaño del buffer si es necesario
                if (buffer_size >= buffer_capacity) {
                    buffer_capacity = buffer_capacity == 0 ? 1024 : buffer_capacity * 2;
                    buffer_temp = realloc(buffer_temp, buffer_capacity);
                }
                
                // Guardar el byte completo en el buffer
                buffer_temp[buffer_size++] = byte_actual;
                bytes_escritos++;
                
                // Reiniciar byte_actual y bits_en_byte para el próximo byte
                byte_actual = 0;
                bits_en_byte = 0;
            }
        }
    }

    // Guardar el número de bits que quedaron en el último byte (si no está completo)
    *bits_finales = bits_en_byte;

    // Si hay bits pendientes en el último byte
    if (bits_en_byte > 0) {
        // Desplazar los bits restantes a la izquierda para completar el byte
        byte_actual <<= (8 - bits_en_byte);
        
        // Aumentar tamaño del buffer si es necesario
        if (buffer_size >= buffer_capacity) {
            buffer_capacity = buffer_capacity == 0 ? 1024 : buffer_capacity * 2;
            buffer_temp = realloc(buffer_temp, buffer_capacity);
        }
        
        // Guardar el último byte en el buffer
        buffer_temp[buffer_size++] = byte_actual;
        bytes_escritos++;
    }

    // Cerrar el archivo de entrada
    fclose(entrada);
    
    // Bloquear acceso al archivo de salida para evitar conflictos (multihilo)
    pthread_mutex_lock(&archivo_mutex);
    
    // Escribir todo el buffer comprimido en el archivo de salida
    fwrite(buffer_temp, 1, buffer_size, salida);
    
    // Desbloquear el acceso al archivo de salida
    pthread_mutex_unlock(&archivo_mutex);
    
    // Liberar la memoria del buffer temporal
    free(buffer_temp);
    
    // Devolver el número total de bytes escritos
    return bytes_escritos;
}

// =========================================================================
// Función: annadir_metadata_al_huff
// Encargada de: Añadir metadatos y tabla de códigos Huffman al final del archivo comprimido
// Entradas:
//   - nombre_archivo: Nombre del archivo .huff donde se guardará la metadata
//   - rutas: Array de estructuras Ruta con los códigos Huffman
//   - num_rutas: Cantidad de códigos en el array
//   - metadatos: Array de metadatos de los archivos comprimidos
//   - total_archivos: Número total de archivos procesados
// Salida: void (escribe directamente en el archivo)
// =========================================================================

void annadir_metadata_al_huff(const char *nombre_archivo, Ruta *rutas, int num_rutas, MetaArchivo *metadatos, int total_archivos) {
    // Ordenar metadatos por nombre de archivo para consistencia (mismo orden siempre)
    qsort(metadatos, total_archivos, sizeof(MetaArchivo), comparar_meta_por_nombre);
    
    // Abrir archivo en modo append binario (añadir al final)
    FILE *f = fopen(nombre_archivo, "ab");
    if (!f) {
        perror("No se pudo abrir el archivo para escribir tabla y metadata");
        return;
    }

    // Obtener posición actual en archivo (inicio de la metadata)
    // Esto servirá como referencia para descomprimir
    long offset_tabla = ftell(f);

    // Escribir tabla de códigos Huffman (byte -> código)
    for (int i = 0; i < num_rutas; i++) {
        // Formato: código_huffman,byte (ej: "101,65" para 'A')
        fprintf(f, "%s,%d\n", rutas[i].ruta, rutas[i].byte);
    }

    // Escribir separador entre tabla de códigos y metadatos
    fprintf(f, "---\n");

    // Escribir metadatos de cada archivo comprimido
    for (int i = 0; i < total_archivos; i++) {
        // Formato: nombre,tamaño_comprimido,bits_útiles_último_byte
        fprintf(f, "%s,%u,%u\n", metadatos[i].nombre, metadatos[i].tam_comprimido, metadatos[i].bits_finales);
    }

    // Escribir el offset al inicio de la metadata al final del archivo
    // Esto permite ubicar rápidamente la metadata al descomprimir
    fwrite(&offset_tabla, sizeof(long), 1, f); 

    // Cerrar archivo
    fclose(f);
}

// =========================================================================
// Función: comprimir_archivo_thread
// Encargada de: Función ejecutada por cada hilo para comprimir un archivo individual
// Entradas:
//   - arg: Puntero genérico que se convierte a DatosHilo* con la información necesaria
// Salida: void* (NULL en este caso, los resultados se escriben en los recursos compartidos)
// =========================================================================

void* comprimir_archivo_thread(void *arg) {
    // Convertir argumento a la estructura de datos del hilo
    DatosHilo *datos = (DatosHilo*)arg;
    
    // Extraer solo el nombre del archivo (sin la ruta completa)
    const char *nombre_archivo = strrchr(datos->ruta_completa, '/');  // Buscar último '/'
    nombre_archivo = nombre_archivo ? nombre_archivo + 1 : datos->ruta_completa;  // Si no encuentra '/', usar nombre completo
    
    // Variables para almacenar resultados de la compresión
    unsigned char bits_finales = 0;      // Bits útiles en el último byte
    unsigned int tam_comprimido = 0;     // Tamaño total comprimido
    
    // Comprimir el archivo (función principal)
    tam_comprimido = comprimir_archivo_y_guardar(
        datos->archivo_comprimido,     // Archivo compartido donde escribir
        datos->ruta_completa,          // Ruta del archivo a comprimir
        datos->codigos,                // Tabla de códigos Huffman
        &bits_finales                  // Puntero para obtener bits finales
    );
    
    // SECCIÓN CRÍTICA (acceso a recursos compartidos)
    // Adquirir mutex para modificar los metadatos compartidos
    pthread_mutex_lock(&metadatos_mutex);
    
    // Guardar resultados en la estructura de metadatos
    strcpy(datos->metadato->nombre, nombre_archivo);          // Nombre del archivo
    datos->metadato->tam_comprimido = tam_comprimido;         // Tamaño comprimido
    datos->metadato->bits_finales = bits_finales;             // Bits útiles finales
    
    // Liberar mutex
    pthread_mutex_unlock(&metadatos_mutex);
    
    // Terminar hilo correctamente
    pthread_exit(NULL);
}

// =========================================================================
// Función: comprimir_directorio
// Encargada de: Coordinar la compresión paralela de todos los archivos en un directorio usando el algoritmo Huffman
// Entradas: ruta_directorio - cadena con la ruta del directorio a comprimir
// Salida: void - genera archivos .huff (datos comprimidos) y .meta (metadatos)
// =========================================================================

void comprimir_directorio(const char *ruta_directorio)
{
    // Arreglo para almacenar frecuencias de cada byte (0-255)
    int frecuencias[256];
    
    // Contar frecuencias de bytes en todos los archivos del directorio
    contar_frecuencias_en_directorio(ruta_directorio, frecuencias);

    // Construir árbol de Huffman
    NodoArbol **lista_de_arboles = NULL;  // Lista dinámica de nodos
    int num_nodos = 0;                    // Contador de nodos
    
    // Crear lista inicial de árboles (hojas) para cada byte con frecuencia > 0
    crear_lista_arboles(frecuencias, &lista_de_arboles, &num_nodos);
    
    // Ordenar nodos por frecuencia (ascendente) para construcción del árbol
    qsort(lista_de_arboles, num_nodos, sizeof(NodoArbol *), comparar_nodos);
    
    // Construir árbol de Huffman a partir de los nodos ordenados
    NodoArbol *raiz_huffman = contruir_arbol_de_huffman(lista_de_arboles, num_nodos);

    // Obtener rutas (códigos Huffman) para cada byte
    Ruta *rutas = NULL;      // Arreglo dinámico de rutas
    int num_rutas = 0;       // Contador de rutas
    
    // Generar códigos Huffman recorriendo el árbol
    rutasHojas(raiz_huffman, &rutas, &num_rutas);

    // LIMPIAR EL ARBOL - liberar memoria del árbol ya que no se necesita más
    limpiar_arbol(raiz_huffman);
    free(lista_de_arboles);  // Liberar lista de nodos

    // Generar tabla de códigos rápida (array indexado por byte)
    char *codigos[256] = {0};  // Inicializar todos a NULL
    
    // Llenar tabla de códigos para acceso rápido O(1)
    for (int i = 0; i < num_rutas; i++)
    {
        codigos[rutas[i].byte] = rutas[i].ruta;  // Asignar código Huffman al byte correspondiente
    }
    
    // Preparar nombres de archivos de salida
    char copia_ruta[MAX_RUTA]; 
    
    // Copiar ruta para manipulación segura
    strncpy(copia_ruta, ruta_directorio, sizeof(copia_ruta) - 1);
    copia_ruta[sizeof(copia_ruta) - 1] = '\0';  // Asegurar terminación nula
    
    // Obtener solo el nombre del directorio (sin ruta completa)
    const char *nombre_directorio = strrchr(copia_ruta, '/'); // Buscar último '/'
    
    // Ajustar puntero para obtener solo el nombre
    if (nombre_directorio) {
        nombre_directorio++;  // Saltar el '/'
    } else {
        nombre_directorio = ruta_directorio;  // Usar ruta completa si no hay '/'
    }
    
    // Generar nombre para archivo de metadatos
    char nombre_archivo_meta[MAX_RUTA];
    snprintf(nombre_archivo_meta, sizeof(nombre_archivo_meta), "%s.meta", nombre_directorio);
    
    // Generar nombre para archivo comprimido
    char nombre_archivo_huff[MAX_RUTA];
    snprintf(nombre_archivo_huff, sizeof(nombre_archivo_huff), "%s.huff", nombre_directorio);
    
    // Abrir archivo de salida en modo escritura binaria
    FILE *archivo_comprimido = fopen(nombre_archivo_huff, "wb");
    if (!archivo_comprimido)
    {
        perror("No se pudo crear el archivo para compresión.");
        return;
    }

    // Inicializar los mutex para sincronización de hilos
    pthread_mutex_init(&archivo_mutex, NULL);    // Para acceso al archivo comprimido
    pthread_mutex_init(&metadatos_mutex, NULL);  // Para acceso a metadatos

    // Lista de metadatos por archivo
    MetaArchivo *metadatos = NULL;
    int total_archivos = 0;

    // Recorrer archivos del directorio para contar cuántos hay
    DIR *dir = opendir(ruta_directorio);
    if (!dir)
    {
        perror("No se pudo abrir el directorio.");
        fclose(archivo_comprimido);
        return;
    }

    // Contar archivos primero para reservar memoria
    struct dirent *entrada;
    int num_archivos = 0;
    while ((entrada = readdir(dir)) != NULL)
    {
        // Ignorar directorios especiales . y ..
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;
        num_archivos++;
    }
    closedir(dir);

    // Crear array de metadatos con memoria dinámica
    metadatos = calloc(num_archivos, sizeof(MetaArchivo));
    if (!metadatos) {
        perror("Error al asignar memoria para metadatos");
        fclose(archivo_comprimido);
        return;
    }

    // Preparar para procesamiento con hilos - abrir directorio nuevamente
    dir = opendir(ruta_directorio);
    if (!dir)
    {
        perror("No se pudo abrir el directorio.");
        free(metadatos);
        fclose(archivo_comprimido);
        return;
    }

    // Preparar datos para los hilos
    DatosHilo *datos_hilos = calloc(num_archivos, sizeof(DatosHilo));  // Datos para cada hilo
    pthread_t *hilos = calloc(num_archivos, sizeof(pthread_t));        // IDs de hilos
    
    // Verificar asignación de memoria
    if (!datos_hilos || !hilos) {
        perror("Error al asignar memoria para hilos");
        free(metadatos);
        fclose(archivo_comprimido);
        closedir(dir);
        if (datos_hilos) free(datos_hilos);
        if (hilos) free(hilos);
        return;
    }

    // Configurar y lanzar hilos
    int idx = 0;  // Índice para archivos/hilos
    while ((entrada = readdir(dir)) != NULL)
    {
        // Ignorar directorios especiales
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0)
            continue;
            
        // Preparar datos para el hilo
        // Construir ruta completa del archivo
        snprintf(datos_hilos[idx].ruta_completa, sizeof(datos_hilos[idx].ruta_completa), 
                "%s/%s", ruta_directorio, entrada->d_name);
        
        // Asignar archivo de salida compartido
        datos_hilos[idx].archivo_comprimido = archivo_comprimido;
        
        // Asignar metadato correspondiente a este archivo
        datos_hilos[idx].metadato = &metadatos[idx];
        
        // Copiar tabla de códigos (punteros) para acceso rápido en el hilo
        for (int i = 0; i < 256; i++) {
            datos_hilos[idx].codigos[i] = codigos[i];
        }
        
        // Crear el hilo para comprimir este archivo
        if (pthread_create(&hilos[idx], NULL, comprimir_archivo_thread, &datos_hilos[idx]) != 0) {
            perror("Error al crear hilo");
            
            // Si falla la creación del hilo, procesar secuencialmente
            unsigned char bits_finales = 0;
            char ruta_completa[512];
            snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", ruta_directorio, entrada->d_name);
            
            // Comprimir archivo de forma secuencial
            unsigned int tam_comprimido = comprimir_archivo_y_guardar(
                archivo_comprimido, ruta_completa, codigos, &bits_finales);
                
            // Guardar metadatos
            strcpy(metadatos[idx].nombre, entrada->d_name);
            metadatos[idx].tam_comprimido = tam_comprimido;
            metadatos[idx].bits_finales = bits_finales;
        }
        
        total_archivos++;  // Incrementar contador de archivos procesados
        idx++;             // Mover al siguiente índice
    }
    
    closedir(dir);  // Cerrar directorio

    // Esperar a que todos los hilos terminen (sincronización)
    for (int i = 0; i < total_archivos; i++) {
        pthread_join(hilos[i], NULL);
    }

    // Liberar recursos de hilos
    free(hilos);
    free(datos_hilos);
    
    // Destruir mutex (liberar recursos de sincronización)
    pthread_mutex_destroy(&archivo_mutex);
    pthread_mutex_destroy(&metadatos_mutex);

    // Cerrar archivo comprimido
    fclose(archivo_comprimido);
   
    // Añadir metadata al final del archivo comprimido
    annadir_metadata_al_huff(nombre_archivo_huff, rutas, num_rutas, metadatos, total_archivos);
    
    // Liberar memoria de metadatos
    free(metadatos);
}



// =========================================================================
// Nombre de la función: main
// Encargada de: Punto de entrada del programa, maneja argumentos y ejecuta compresión
// Entradas: argc y argv desde línea de comandos
// Salida: 0 si éxito, 1 si error
// =========================================================================

int main(int argc, char *argv[])
{
    // Validar que se haya pasado al menos un argumento (el directorio)
    if (argc < 2)
    {
        fprintf(stderr, "Error: Falta el argumento de directorio\nUso: %s <directorio>\n", argv[0]);
        return 1; // Finaliza el programa con código de error
    }

    // Validar que no se hayan pasado más argumentos de los necesarios
    if (argc > 2)
    {
        fprintf(stderr, "Error: Demasiados argumentos\nUso: %s <directorio>\n", argv[0]);
        return 1; // Finaliza el programa con código de error
    }
    
    // Guardar la ruta del directorio recibida en una variable
    const char *ruta_directorio = argv[1];

    // Preparar una versión "limpia" de la ruta
    char ruta_directorio_limpia[PATH_MAX];
    // Copiar la ruta recibida al buffer de ruta limpia, asegurándose de no desbordar
    strncpy(ruta_directorio_limpia, argv[1], sizeof(ruta_directorio_limpia) - 1);
    // Asegurar que el string esté terminado en nulo
    ruta_directorio_limpia[sizeof(ruta_directorio_limpia) - 1] = '\0';
    // Obtener la longitud de la ruta limpia
    size_t len = strlen(ruta_directorio_limpia);
    // Si la ruta termina en '/', eliminarla
    if (len > 0 && ruta_directorio_limpia[len - 1] == '/') {
        ruta_directorio_limpia[len - 1] = '\0'; 
    }
   
    // Intentar abrir el directorio especificado
    DIR *directorio = opendir(ruta_directorio_limpia);
    if (!directorio)
    {
        // Si falla al abrir el directorio, mostrar error y terminar
        perror("Error abriendo el directorio");
        exit(1);
    }
    
    // Variable para indicar si el directorio está vacío
    int vacio = 1;
    struct dirent *elemento;
    // Leer los elementos dentro del directorio uno por uno
    while ((elemento = readdir(directorio)) != NULL)
    {
        // Ignorar las entradas "." y ".."
        if (strcmp(elemento->d_name, ".") != 0 && strcmp(elemento->d_name, "..") != 0)
        {
            vacio = 0; // Encontró al menos un archivo o carpeta
            break;     // Ya no necesita seguir buscando
        }
    }

    // Cerrar el directorio abierto
    closedir(directorio);

    // Si el directorio estaba vacío, mostrar error y terminar
    if (vacio)
    {
        fprintf(stderr, "El directorio no tiene archivos. %s\n", ruta_directorio);
        exit(1);
    }
    printf("\nCompresor con hilos: \n");
    printf("\nCompresión iniciada por favor espere... \n");
    
   
    

    struct timespec inicio, fin;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    comprimir_directorio(ruta_directorio_limpia);

    clock_gettime(CLOCK_MONOTONIC, &fin);

    // Calcular la diferencia en nanosegundos
    long segundos = fin.tv_sec - inicio.tv_sec;
    long nanosegundos = fin.tv_nsec - inicio.tv_nsec;
    long long tiempo_total_ns = segundos * 1000000000LL + nanosegundos;
    double tiempo_total_ms = tiempo_total_ns / 1e6;

    printf("Tiempo tardado: %lld nanosegundos (%.3f milisegundos)\n", tiempo_total_ns, tiempo_total_ms);
    
    // Finalizar el programa exitosamente
    return 0;
}
