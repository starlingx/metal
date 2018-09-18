#ifndef __INCLUDE_KEYCLASS_H__
#define __INCLUDE_KEYCLASS_H__
/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include <sys/types.h>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <errno.h>


using namespace std;

// #include "nodeBase.h"

#define MAX_KEYS 100

class keyClass
{
    private:

    /** 
     *  A single key:value pair within the keyClass.
     *  Used to build a linked list of key value pairs.
     */
    struct keyValue {

        string          key__str  ;
        unsigned long * val__ptr ; /* The value for the string key is a unsigned long pointer */
        
        unsigned long key__int ;
        unsigned long val__int ;

        struct keyValue * prev;
        struct keyValue * next;
    } ;

    struct keyValue * head ; 
    struct keyValue * tail ;

    /** Allocate memory for a new key. */
    struct keyClass::keyValue * newKey ( void );

    struct keyClass::keyValue * addKey (        string key__str, unsigned long * val__ptr );
    struct keyClass::keyValue * addKey ( unsigned long key__int, unsigned long   val__int );

    struct keyClass::keyValue * getKey (        string key__str );
    struct keyClass::keyValue * getKey ( unsigned long key__int );

    int                         remKey ( struct keyClass::keyValue * keyValue_ptr );
    int                         delKey ( struct keyClass::keyValue * keyValue_ptr );

    keyClass::keyValue * key_ptrs[MAX_KEYS] ;
        
    int memory_allocs ;
    int memory_used ;

public:
    
     keyClass(); /**< constructor */	
    ~keyClass(); /**< destructor  */

    int keys ;

    int get_key ( unsigned long key__int , unsigned long  & val__int );
    int get_key (        string key__str , unsigned long *& val__ptr );
    int add_key ( unsigned long key__int , unsigned long    val__int );
    int add_key (        string key__str , unsigned long *  val__ptr );
    int del_key ( unsigned long key__int );
    int del_key (        string key__str );
} ;

keyClass * get_keyClass_ptr ( void );

#define GET_KEYVALUE_PTR(key)                                          \
    keyClass * keyClass_ptr = get_keyClass_ptr () ;                    \
    keyClass::keyValue * keyValue_ptr = keyClass_ptr->getKey ( key ) ; \
    if ( keyValue_ptr == NULL )                                        \
    {                                                                  \
        elog ("key %ld not fault\n", hostname.c_str());                \
        return (FAIL_HOSTNAME_LOOKUP);                                 \
    }

#endif // __INCLUDE_KEYCLASS_H__
