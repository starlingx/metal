/*
 * Copyright (c) 2015 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/* This module implements a linked list for unsigned long key:value pairs.
 * 
 * Was first introduced to provide to associate a libEvent address with a
 * eventLib base pointer so that the event handler could lookup the event
 * from the base, Key is base and value is the libEvent address.
 *
 */

#ifdef  __AREA__
#undef  __AREA__
#define __AREA__ "kvp"
#endif

#include "nodeBase.h"
#include "keyClass.h"
//#include "nodeUtil.h"


/* keyClass Constructor */
keyClass::keyClass() 
{
    head = tail   = NULL;
    memory_allocs = 0   ;
    memory_used   = 0   ;
    keys          = 0   ;
  
    for ( int i = 0 ; i < MAX_KEYS ; i++ )
    {
        key_ptrs[i] = NULL ;
    }
}

/* keyClass Destructor */
keyClass::~keyClass() 
{
    for ( int i = 0 ; i < MAX_KEYS ; i++ )
    {
        if ( key_ptrs[i] )
        {
            delete key_ptrs[i] ;
        }
    }
}

/******************************************************************************************
 **********    C O M M O N      P R I V A T E       I N T E R F A C E S     ***************
 *****************************************************************************************/

/******************************************************************
 *
 * Name       : newKey     (private)
 *
 * Description: Allocates memory for a new key value pair and 
 *              stores its the address in key_ptrs 
 *
 * @param void
 * @return pointer to the newly allocted Key memory
 *
 *******************************************************************/ 
struct keyClass::keyValue * keyClass::newKey ( void )
{
   struct keyClass::keyValue * key_ptr = NULL ;
      
   // find an empty spot
   for ( int i = 0 ; i < MAX_KEYS ; i++ )
   {
      if ( key_ptrs[i] == NULL )
      {
          key_ptrs[i] = key_ptr = new keyValue ;
          memory_allocs++ ;
          memory_used += sizeof (struct keyClass::keyValue);
          keys++ ;
          return key_ptr ;
      }
   }
   elog ( "Failed to save new key pointer address\n" );
   return key_ptr ;
}


/**************************************************************************
 *
 * Name       : delKey      (private)
 *
 * Description: Frees the memory of a pre-allocated key and removes
 *              it from the key_ptrs list.
 *
 * @param key * pointer to the key memory address to be freed
 * @return int return code { PASS or -EINVAL }
 *
 **************************************************************************/
int keyClass::delKey ( struct keyClass::keyValue * key_ptr )
{
    if ( memory_allocs > 0 )
    {
        for ( int i = 0 ; i < MAX_KEYS ; i++ )
        {
            if ( key_ptrs[i] == key_ptr )
            {
                delete key_ptr ;
                key_ptrs[i] = NULL ;
                memory_used -= sizeof (struct keyClass::keyValue);
                if ( memory_allocs ) memory_allocs-- ;
                if ( keys ) keys-- ;
                return PASS ;
            }
        }
        elog ( "Unable to validate memory address being freed\n" );
    }
    else
       elog ( "Free memory called when there is no memory to free\n" );
    
    return -EINVAL ;
}

/*****************************************************************
 *
 * Name       : remKey
 *
 * Description: Remove a specified key from the linked list
 *
 *@param unsigned long key signature
 *@return PASS or failure codes ENODEV, ENXIO, EFAULT
 *
 *****************************************************************/
int keyClass::remKey( struct keyValue * ptr )
{
    if ( head == NULL )
        return -ENXIO ;
    
    if ( ptr == NULL )
        return -EFAULT ;
    
    /* If the key is the head key */
    if ( ptr == head )
    {
        /* only one key in the list case */
        if ( head == tail )
        {
            dlog3 ("Single Key -> Head Case\n");
            head = NULL ;
            tail = NULL ;
        }
        else
        {
            dlog3 ("Multiple Keys -> Head Case\n");
            head = head->next ;
            head->prev = NULL ; 
        }
    }
    /* if not head but tail then there must be more than one
     * key in the list so go ahead and chop the tail.
     */
    else if ( ptr == tail )
    {
        dlog3 ("Multiple Key -> Tail Case\n");
        tail = tail->prev ;
        tail->next = NULL ;
    }
    else
    {
        dlog3 ("Multiple Key -> Full Splice Out\n");
        ptr->prev->next = ptr->next ;
        ptr->next->prev = ptr->prev ;
    }
    delKey ( ptr );
                
    return (PASS) ;
}



/******************************************************************************************
 **********    K E Y    T Y P E   P R I V A T E       I N T E R F A C E S     *************
 *****************************************************************************************/

/******************************************************************
 *
 * Name       : addKey        (private)
 *
 * Description: Reprovisions the key:value if it already exists
 *              Calls newkey to allocates memory for a new key
 *              and then tacks the new key value onto the end
 *              of the linked list.
 *
 * @param unsigned long key and value
 * @return pointer to the newly allocted Key memory
 *
 *******************************************************************/ 
struct keyClass::keyValue* keyClass::addKey( unsigned long key__int, unsigned long val__int )
{
    /* verify key is not already provisioned */
    struct keyValue * ptr = getKey ( key__int );
    if ( ptr )
    {
        if ( delKey ( ptr ) )
        {
            /* Should never get here but if we do then */
            /* something is seriously wrong */
            elog ("Unable to remove key during reprovision\n"); 
            return static_cast<struct keyValue *>(NULL);
        }
    }
    
    /* allocate memory for new key */
    ptr = newKey ();
    if( ptr == NULL )
    {
        elog ( "Failed to allocate memory for new key\n" );
		return static_cast<struct keyValue *>(NULL);
    }
    
    /* push the calling data into new key */
    ptr->key__int = key__int ;
    ptr->val__int = val__int ;
    
    dlog3 ("add key:%lx val:%lx\n", ptr->key__int, ptr->val__int );

    /* If the key list is empty add it to the head */
    if( head == NULL )
    {
        head = ptr ;  
        tail = ptr ;
        ptr->prev = NULL ;
        ptr->next = NULL ;
    }
    else
    {
        /* link the new_key to the tail of the key_list
         * then mark the next field as the end of the key_list
         * adjust tail to point to the last key
         */
        tail->next = ptr  ;
        ptr->prev  = tail ;
        ptr->next  = NULL ;
        tail = ptr ; 
    }
    return ptr ;
}

/******************************************************************
 *
 * Name       : getKey        (private)
 *
 * Description: Looks  for a key in the linked list.
 *
 * @param unsigned long key
 * @return pointer to the matching key in the linked list
 *
 *******************************************************************/ 
struct keyClass::keyValue * keyClass::getKey ( unsigned long key__int )
{
   /* check for empty list condition */
   if ( head == NULL )
      return NULL ;

   for ( struct keyValue * ptr = head ;  ; ptr = ptr->next )
   {
       if ( ptr->key__int == key__int ) 
       {
           dlog3 ("get key:%lx val:%lx\n", ptr->key__int, ptr->val__int );
           return ptr ;  
       }
       if (( ptr->next == NULL ) || ( ptr == tail ))
           break ;
    }
    return static_cast<struct keyValue *>(NULL);
}

/*****************************************************************/
/* String key version                                            */
/******************************************************************
 *
 * Name       : addKey        (private)
 *
 * Description: Reprovisions the key:value if it already exists
 *              Calls newkey to allocates memory for a new key
 *              and then tacks the new key value onto the end
 *              of the linked list.
 *
 * @param unsigned long key and value
 * @return pointer to the newly allocted Key memory
 *
 *******************************************************************/ 
struct keyClass::keyValue* keyClass::addKey( string key__str, unsigned long * val__ptr )
{
    /* verify key is not already provisioned */
    struct keyValue * ptr = getKey ( key__str );
    if ( ptr )
    {
        if ( delKey ( ptr ) )
        {
            /* Should never get here but if we do then */
            /* something is seriously wrong */
            elog ("Unable to remove key during reprovision\n"); 
            return static_cast<struct keyValue *>(NULL);
        }
    }
    
    /* allocate memory for new key */
    ptr = newKey ();
    if( ptr == NULL )
    {
        elog ( "Failed to allocate memory for new key\n" );
		return static_cast<struct keyValue *>(NULL);
    }
    
    /* push the calling data into new key */
    ptr->key__str = key__str ;
    ptr->val__ptr = val__ptr ;
    
    dlog3 ("add key:%s val:%p\n", ptr->key__str.c_str(), ptr->val__ptr );

    /* If the key list is empty add it to the head */
    if( head == NULL )
    {
        head = ptr ;  
        tail = ptr ;
        ptr->prev = NULL ;
        ptr->next = NULL ;
    }
    else
    {
        /* link the new_key to the tail of the key_list
         * then mark the next field as the end of the key_list
         * adjust tail to point to the last key
         */
        tail->next = ptr  ;
        ptr->prev  = tail ;
        ptr->next  = NULL ;
        tail = ptr ; 
    }
    return ptr ;
}


/******************************************************************
 *
 * Name       : getKey        (private)
 *
 * Description: Looks  for a key in the linked list.
 *
 * @param unsigned long key
 * @return pointer to the matching key in the linked list
 *
 *******************************************************************/ 
struct keyClass::keyValue * keyClass::getKey ( string key__str )
{
   /* check for empty list condition */
   if ( head == NULL )
      return NULL ;

   for ( struct keyValue * ptr = head ;  ; ptr = ptr->next )
   {
       if ( ptr->key__str == key__str ) 
       {
           dlog3 ("get key:%s val:%p\n", ptr->key__str.c_str(), ptr->val__ptr );
           return ptr ;  
       }
       if (( ptr->next == NULL ) || ( ptr == tail ))
           break ;
    }
    return static_cast<struct keyValue *>(NULL);
}

/******************************************************************************************
 ****************     P U B L I C      I N T E R F A C E S     ****************************
 *****************************************************************************************/ 

/******************************************************************************************
 *
 * Name    : get_key
 *
 * Purpose : look up the alue based on a key
 *
 ******************************************************************************************/

/*
 *   Unsigned Long K E Y
 *   Unsigned Long V A L u e
 */
int keyClass::get_key ( unsigned long key__int, unsigned long & val__int )
{
    /* verify key is not already provisioned */
    struct keyValue * keyValue_ptr = getKey ( key__int );
    if ( keyValue_ptr )
    {
        val__int = keyValue_ptr->val__int ;
        return (PASS);
    }
    return(FAIL_NOT_FOUND);
}

/*
 *   String                K E Y
 *   Unsigned Long Pointer V A L u e
 */
int keyClass::get_key ( string key__str, unsigned long *& val__ptr )
{
    /* verify key is not already provisioned */
    struct keyValue * keyValue_ptr = getKey ( key__str );
    if ( keyValue_ptr )
    {
        val__ptr = keyValue_ptr->val__ptr ;
        return (PASS);
    }
    return(FAIL_NOT_FOUND);
}


/******************************************************************************************
 *
 * Name    : add_key
 *
 * Purpose : add a key value to the object
 *
 ******************************************************************************************/

/*
 *   Unsigned Long K E Y
 *   Unsigned Long V A L u e
 */

int keyClass::add_key ( unsigned long key__int, unsigned long val__int )
{
    int rc = FAIL ;
    struct keyClass::keyValue * keyValue_ptr = static_cast<struct keyValue *>(NULL);

    keyValue_ptr = keyClass::getKey(key__int);
    if ( keyValue_ptr ) 
    {
        wlog ("add key:%lx failed - in use (value:%lx)\n", keyValue_ptr->key__int, keyValue_ptr->val__int );

        /* Send back a retry in case the add needs to be converted to a modify */
        return (RETRY);
    }
    /* Otherwise add it as a new key */
    else
    {
        keyValue_ptr = keyClass::addKey( key__int , val__int);
        if ( keyValue_ptr )
        {
            dlog3 ("add key:%lx\n", key__int ); 
            rc = PASS ;
        }
        else
        {
            rc = FAIL_NULL_POINTER ;
        }
    }
    return (rc);
}

/*
 *   String                K E Y
 *   Unsigned Long Pointer V A L u e
 */
int keyClass::add_key ( string key__str, unsigned long * val__ptr )
{
    int rc = FAIL ;
    struct keyClass::keyValue * keyValue_ptr = static_cast<struct keyValue *>(NULL);

    keyValue_ptr = keyClass::getKey(key__str);
    if ( keyValue_ptr ) 
    {
        wlog ("add key:%s failed - in use (value:%p)\n", keyValue_ptr->key__str.c_str(), keyValue_ptr->val__ptr );

        return (RETRY);
    }
    /* Otherwise add it as a new key */
    else
    {
        keyValue_ptr = keyClass::addKey( key__str, val__ptr);
        if ( keyValue_ptr )
        {
            dlog3 ("add key:%s\n", key__str.c_str()); 

            rc = PASS ;
        }
        else
        {
            rc = FAIL_NULL_POINTER ;
        }
    }
    return (rc);
}


/******************************************************************************************
 *
 * Name    : del_key
 *
 * Purpose : del a key value from the object
 *
 ******************************************************************************************/

/*
 *   Unsigned Long K E Y
 *   Unsigned Long V A L u e
 */


int keyClass::del_key ( unsigned long key__int )
{
    int rc = FAIL_DEL_UNKNOWN ;
    keyClass::keyValue * keyValue_ptr = keyClass::getKey( key__int );
    if ( keyValue_ptr ) 
    {
        rc = remKey ( keyValue_ptr );
        if ( rc != PASS )
        {
            elog ("del key:%lx failed - rc:%d\n", key__int , rc );
        }
        else
        {
            dlog3 ("del key:%lx\n", key__int ); 
        }
    }
    else
    {
        wlog ("del key:%lx failed - not found\n", key__int );
    }
    return (rc);
}

/*
 *   String                K E Y
 *   Unsigned Long Pointer V A L u e
 */
int keyClass::del_key ( string key__str )
{
    int rc = FAIL_DEL_UNKNOWN ;
    keyClass::keyValue * keyValue_ptr = keyClass::getKey( key__str );
    if ( keyValue_ptr ) 
    {
        rc = remKey ( keyValue_ptr );
        if ( rc != PASS )
        {
            elog ("del key:%s failed - rc:%d\n", key__str.c_str(), rc );
        }
        else
        {
            dlog3 ("del key:%s\n", key__str.c_str()); 
        }
    }
    else
    {
        wlog ("del key:%s failed - not found\n", key__str.c_str());
    }
    return (rc);
}
