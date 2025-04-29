#include <stdlib.h>
#include "funciones_arboles.h"
#include <stdio.h>
#include <string.h> 

//Crear una lista de nodos de arbol, los cuales si aparezcan en los archivos a comprimir.
//Se asigna al argumento lista de árboles.
void crear_lista_arboles(int frecuencias[256], NodoArbol ***lista_de_arboles, int *num_nodos)
{
    int num_nodos2 = 0;

    // Saber cuantos bytes tienen frecuencia mayor a 0
    for (int i = 0; i < 256; i++)
    {
        if (frecuencias[i] > 0)
        {
            (*num_nodos)++;
            num_nodos2++;
        }
    }

    *lista_de_arboles = calloc(num_nodos2, sizeof(NodoArbol *));

    if (lista_de_arboles == NULL)
    {
        perror("Error al asignar memoria");
        exit(1);
    }
    int index = 0;
    for (int i = 0; i < 256; i++)
    {
        if (frecuencias[i] > 0)
        {
            (*lista_de_arboles)[index] = calloc(1, sizeof(NodoArbol));
            (*lista_de_arboles)[index]->frecuencia = frecuencias[i];
            (*lista_de_arboles)[index]->byte = i;

            index++;
        }
    }
}

//Insertar los nodos en orden en el arbol.
//De manera que las rutas sean validas
//Se obtiene el árbol de huffman en el argumento Nuevo
void insertar_ordenar(NodoArbol **lista_arboles, int *num_nodos, NodoArbol *nuevo)
{
    int low = 0, hi = *num_nodos;
    while (low < hi)
    {
        int mid = (low + hi) / 2;
        if (nuevo->frecuencia < lista_arboles[mid]->frecuencia)
        {
            hi = mid;
        }
        else
        {
            low = mid + 1;
        }
    }
    for (int i = *num_nodos; i > low; i--)
    {
        lista_arboles[i] = lista_arboles[i - 1];
    }

    // Insertar el nuevo nodo en la posición correcta
    lista_arboles[low] = nuevo;
    (*num_nodos)++;
}

//COnstruye el arbol de huffman
//Combina nodos y deja el árbol de manera en que los nodos mas frecuente esten a un nivel mayor.
NodoArbol *contruir_arbol_de_huffman(NodoArbol **lista_de_arboles, int num_nodos)
{
    while (num_nodos > 1)
    {
        NodoArbol *izquierdo = lista_de_arboles[0];
        NodoArbol *derecho = lista_de_arboles[1];

        NodoArbol *nuevo = calloc(1, sizeof(NodoArbol));
        nuevo->frecuencia = izquierdo->frecuencia + derecho->frecuencia;
        nuevo->byte = -1; // Los nodos con byte -1 son internos.
        nuevo->izquierdo = izquierdo;
        nuevo->derecho = derecho;

        // Eliminar los ultimos 2 nodos.
        // No se eliminar realmente pero se dejan de considerar.
        for (int i = 2; i < num_nodos; i++)
        {
            lista_de_arboles[i - 2] = lista_de_arboles[i];
        }
        num_nodos -= 2;
        insertar_ordenar(lista_de_arboles, &num_nodos, nuevo);
    }

    return lista_de_arboles[0]; // Devolver la raiz.
}

void limpiar_arbol(NodoArbol *raiz_huffman)
{

    if (raiz_huffman == NULL){
        return;
    }

    limpiar_arbol(raiz_huffman->izquierdo);
    limpiar_arbol(raiz_huffman->derecho);

    free(raiz_huffman);
}


//Dado el árbol recorre todas las rutas posibles, y escribe el camino para llegar a ese nodo.
//Es decir si para una hoja, tuvo que irse izquierda, derecha, izquierda, en la raíz tendra como ruta 010
void rutasHojas(NodoArbol *raiz, Ruta **rutas, int *num_rutas)
{
    if (!raiz)
        return;

    *rutas = NULL;
    *num_rutas = 0;

    Cola cola;
    inicializarCola(&cola);
    push(&cola, raiz, "");

    while (cola.frente)
    {
        NodoCola *actual = pop(&cola);
        NodoArbol *nodo = actual->nodo;
        char *ruta_actual = actual->ruta;

        // Si no tiene hijos, entonces es el byte final
        if (!nodo->izquierdo && !nodo->derecho)
        {
            *rutas = (Ruta *)realloc(*rutas, (*num_rutas + 1) * sizeof(Ruta));
            strcpy((*rutas)[*num_rutas].ruta, ruta_actual);
            (*rutas)[*num_rutas].byte = nodo->byte;
            (*num_rutas)++;
        }
        else
        {
            // Izquierda 0, derecha 1
            if (nodo->izquierdo)
            {
                //Se pone el +2 para evitar un warning, pero nunca ocurrirá el escenario del warning
                //El warning dice que es posible anadirle un byte a la ruta actual, pasando a ser de 256 a 257, dado que es el espacio de ruta actual
                //La ruta nunca sera de mas de 255 en el peor de los casos
                //Sin embargo aún el peor de los casos es improbable
                char nueva_ruta[MAX_RUTA+2];
                snprintf(nueva_ruta, sizeof(nueva_ruta), "%s0", ruta_actual);
                push(&cola, nodo->izquierdo, nueva_ruta);
            }

            if (nodo->derecho)
            {
                char nueva_ruta[MAX_RUTA+2 ];
                snprintf(nueva_ruta, sizeof(nueva_ruta), "%s1", ruta_actual);
                push(&cola, nodo->derecho, nueva_ruta);
            }
        }

        free(actual);
    }
}

void inicializarCola(Cola *cola)
{
    cola->frente = cola->final = NULL;
}

void push(Cola *cola, NodoArbol *nodo, const char *ruta)
{

    NodoCola *nuevo = calloc(1, sizeof(NodoCola));

    nuevo->nodo = nodo;
    strcpy(nuevo->ruta, ruta);
    nuevo->siguiente = NULL;

    if (cola->final)
    {
        cola->final->siguiente = nuevo;
    }
    else
    {
        cola->frente = nuevo;
    }
    cola->final = nuevo;
}

NodoCola *pop(Cola *cola)
{
    if (!cola->frente)
        return NULL;

    NodoCola *nodo = cola->frente;
    cola->frente = cola->frente->siguiente;
    if (!cola->frente)
        cola->final = NULL;

    return nodo;
}


//------ DEFINICIONES UTILIZADAS EN LA DESCOMPRESION 
NodoArbolDecompresion* crearNodo() {
    NodoArbolDecompresion *nuevo = calloc(1,sizeof(NodoArbolDecompresion));
    nuevo->byte = -1;  // Por defecto es interno
    nuevo->izq = NULL;
    nuevo->der = NULL;
    return nuevo;
}

void insertarEnArbol(NodoArbolDecompresion *raiz, const char *codigo, int byte) {
    if (*codigo == '\0') {
        raiz->byte = byte;
        return;
    }
    if (*codigo == '0') {
        if (!raiz->izq) raiz->izq = crearNodo();
        insertarEnArbol(raiz->izq, codigo + 1, byte);
    } else {
        if (!raiz->der) raiz->der = crearNodo();
        insertarEnArbol(raiz->der, codigo + 1, byte);
    }
}

NodoArbolDecompresion* reconstruirArbol(Codigo *codigosProcesados, int num_codigos) {
    NodoArbolDecompresion *raiz = crearNodo();  
    for (int i = 0; i < num_codigos; i++) {
        insertarEnArbol(raiz, codigosProcesados[i].ruta, codigosProcesados[i].byte);
    }
    return raiz;
}

void recorrido_arbol_optimizado(FILE *archivo_descomprimido, char **lista_bytes, int num_bytes, NodoArbolDecompresion *arbol, char *ultimo_byte) {
    NodoArbolDecompresion *actual = arbol;
    
    for (int i = 0; i < num_bytes; i++) {
        char *codigo = lista_bytes[i];
        while (*codigo) {
            actual = (*codigo == '0') ? actual->izq : actual->der;
            codigo++;
            
            if (actual->byte != -1) {
                fputc(actual->byte, archivo_descomprimido);
                actual = arbol;
            }
        }
    }
    //Ultimo byte
    if (strcmp(ultimo_byte, "No") != 0) {
        char *codigo = ultimo_byte;
        while (*codigo) {
            actual = (*codigo == '0') ? actual->izq : actual->der;
            codigo++;
            if (actual->byte != -1) {
                fputc(actual->byte, archivo_descomprimido);
                actual = arbol;
            }
        }
    }
}