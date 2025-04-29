#include <stdio.h>
#ifndef FUNCIONES_ARBOLES_H
#define FUNCIONES_ARBOLES_H

#define MAX_RUTA 256 // Longitud maxima de la ruta.
//Estructura del árbol de huffman. Byte es el byte encontrado ejm 10110101.
//Frecuencia es cuantas veces se encontró en el archivo.
typedef struct NodoArbol
{
    int frecuencia;
    int byte;
    struct NodoArbol *izquierdo;
    struct NodoArbol *derecho;
} NodoArbol;
//Estructura para llegar al byte en el arbol de huffman.
//Ruta puede ser 101, Osea derecha, izquierda derecha.
//Byte es el byte que se encontraría en en el nodo hoja.
typedef struct
{
    char ruta[MAX_RUTA]; // 0 es izquierda, 1 es derecha
    int byte;
} Ruta;

//Estructura que representa la información de un item en cola.

typedef struct NodoCola
{
    NodoArbol *nodo;
    char ruta[MAX_RUTA];
    struct NodoCola *siguiente;
} NodoCola;

typedef struct
{
    NodoCola *frente;
    NodoCola *final;
} Cola;

void inicializarCola(Cola *cola);
void push(Cola *cola, NodoArbol *nodo, const char *ruta);
NodoCola *pop(Cola *cola);
//Crear una lista de nodos de arbol, los cuales si aparezcan en los archivos a comprimir.
//Se asigna al argumento lista de árboles.
void crear_lista_arboles(int frecuencias[256], NodoArbol ***lista_de_arboles, int *num_nodos);
//Insertar los nodos en orden en el arbol.
//De manera que las rutas sean validas
//Se obtiene el árbol de huffman en el argumento Nuevo
void insertar_ordenar(NodoArbol **lista_arboles, int *num_nodos, NodoArbol *nuevo);

//COnstruye el arbol de huffman
//Combina nodos y deja el árbol de manera en que los nodos mas frecuente esten a un nivel mayor.
NodoArbol *contruir_arbol_de_huffman(NodoArbol **lista_de_arboles, int num_nodos);

void limpiar_arbol(NodoArbol *raiz_huffman);
//Dado el árbol recorre todas las rutas posibles, y escribe el camino para llegar a ese nodo.
//Es decir si para una hoja, tuvo que irse izquierda, derecha, izquierda, en la raíz tendra como ruta 010
void rutasHojas(NodoArbol *raiz, Ruta **rutas, int *num_rutas);

//COnstruye el arbol de huffman
//Combina nodos y deja el árbol de manera en que los nodos mas frecuente esten a un nivel mayor.
NodoArbol *contruir_arbol_de_huffman(NodoArbol **lista_de_arboles, int num_nodos);




//------ DEFINICIONES UTILIZADAS EN LA DESCOMPRESION 
#define MAX_LINEA 256
typedef struct NodoArbolDecompresion {
    int byte;  // -1 si es interno
    struct NodoArbolDecompresion *izq;
    struct NodoArbolDecompresion *der;
} NodoArbolDecompresion;
typedef struct {
    char ruta[MAX_LINEA];  // Ruta del arbol
    int byte;              // Valor del byte
} Codigo;


NodoArbolDecompresion* crearNodo() ;
void insertarEnArbol(NodoArbolDecompresion *raiz, const char *codigo, int byte) ;
NodoArbolDecompresion* reconstruirArbol(Codigo *codigosProcesados, int num_codigos);
void recorrido_arbol_optimizado(FILE *archivo_descomprimido, char **lista_bytes, int num_bytes, NodoArbolDecompresion *arbol, char *ultimo_byte);
#endif