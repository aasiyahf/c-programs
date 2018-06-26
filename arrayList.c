#include <stdlib.h>
#include <assert.h>
#include "ArrayList.h"

/**
 * arrayList is a custom list which can allow for the storage of lists of objects
 * and acts like a resizable array list
 * @author Aasiyah Feisal (anfeisal)
 *
 */
struct arrayList {
    size_t size;
    void ** data;
};

//Allocates memory for array list
struct arrayList *createList() {
    /* Allocate Memory */
    struct arrayList *list = malloc( sizeof( struct arrayList ) );
    assert( list != NULL );
    list->size = 0;
    list->data = calloc( 2, sizeof( void * ) );
    assert( list->data != NULL );
    list->data[ 0 ] = NULL;
    return list;
}

//Sets data for array list
void setData( struct arrayList *list, void ** data, int max, int clear_data ) {
    /* Sets the internal array of the arraylist */
    clear_data ? clear( list ) : NULL;
    list->data = data;
    list->size = max;
}

//Adds element to array list
void add( struct arrayList *list, void *elem ) {
    /* Adds one element of generic pointer type to the internal array */
    void ** new_data = realloc( list->data, memorySize( list ) );
    assert( new_data != NULL );
    new_data[ list->size ] = elem;
    setData( list, new_data, list->size + 1, 0 );
}

//Retrieves element from array list
void *get( struct arrayList *list, int index ) {
    /* Gets an member of the array at an index */
    return list->data[index];
}

//Returns internal size of array list
size_t memorySize( struct arrayList *list ) {
    /* Returns the size of the internal array in memory */
    return sizeof( *list->data );
}

//Returns length of array list
size_t length( struct arrayList *list ) {
    /* Returns the number of elements in the arraylist */
    return list->size;
}

//Removes element from array list
void remove( struct arrayList *list, int index, int freeit ) {
    /* Removes one element at and index */
    if ( index > list->size - 1 )
        return;
    if ( list->size == 1 ) {
        clear( list );
        return;
    }
    if ( freeit )
        free(get( list, index ) );
    for ( int i = index; i < list->size; ++i ) {
        if ( i == list->size - 1 )
            list->data[ i ] = NULL;
        else
            list->data[ i ] = list->data[ i + 1 ];
    }
    void ** new_data = realloc( list->data, memorySize( list ) );
    --list->size;
    assert( new_data != NULL );
    setData( list, new_data, list->size, 0 );
}

//Clears array list
void clear( struct arrayList *list ) {
    /* Clears the internal array */
    list->size = 0;
    free( list->data );
    list->data = NULL;
}

//Deallocates space for array list
void freeList( struct arrayList *list ) {
    /* De-allocates the arraylist from memory
    No usage of the arraylist is allowed after this function call */
    if ( list->data != NULL )
        free( list->data );
    free( list );
}

//Returns index of element within array list
int getindex( struct arrayList *list, void *elem ) {
    /* Looks for elem in list and returns the index or -1 if not found */
    for( int i = 0; i < list->size; ++i )
        if ( elem == get( list, i ) )
            return i;
    return -1;
}
