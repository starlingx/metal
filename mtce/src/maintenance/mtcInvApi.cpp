/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Controller Maintenance
  * Access to Inventory Database via REST API Interface.
  *
  */

/** This file implements the a set of mtcInvApi utilities that maintenance
  * calls upon to set/get host information to/from the sysinv database.
  *
  * The APIs exposed from this file are
  *
  *   mtcInvApi_read_inventory - Reads all the host inventory records from the
  *                              sysinv database in a specified batch number.
  *   mtcInvApi_add_host       - Adds a host to the sysinv database.
  *   mtcInvApi_load_host      - Loads the inventory content for a specified host.
  *   mtcInvApi_update_task    - Updates the task field of the specified host.
  *   mtcInvApi_update_uptime  - Updates the uptime of the specified host.
  *   mtcInvApi_update_value   - Updates any field of the specified host.
  *   mtcInvApi_update_state   - Updates a maintenance state of specified host.
  *   mtcInvApi_update_states  - Updates all maintenance states of specified host.
  *   mtcInvApi_force_states   - Force updates all maintenance states of specified host.
  *
  *   Each utility is paired with a private handler.
  *
  *   mtcInvApi_get_handler    - handles response for mtcInvApi_read_inventory
  *   mtcInvApi_add_Handler    - handles response for mtcInvApi_add_host
  *   mtcInvApi_qry_handler    - handles response for mtcInvApi_load_host
  *   mtcInvApi_handler        - handles inventory specific response for all update utilities
  *
  * Warning: These calls cannot be nested.
  *
  **/

#ifdef  __AREA__
#undef  __AREA__
#endif
#define __AREA__ "inv"

#include "nodeBase.h"       /* for ... Base Service Header             */
#include "nodeClass.h"      /* for ... maintenance class nodeLinkClass */
#include "nodeUtil.h"       /* for ... Utility Service Header          */
#include "jsonUtil.h"       /* for ... Json utilities                  */
#include "mtcInvApi.h"      /* this .. module header                   */
#include "mtcKeyApi.h"      /* for ... keystone service utilities      */
#include "mtcNodeHdlrs.h"   /* for ... mtcTimer_handler ( .. )         */


/***********************************************************************
 *
 * Name       : mtcInvApi_read_inventory
 *
 * Purpose    : Issue an inventory GET request for a specified batch
 *              number of inventory elements.
 *
 */
int mtcInvApi_read_inventory ( int batch )
{
    char batch_str [10] ;
    int rc    = PASS ;
    int count = 0 ;
    int loops = 0 ;

    nodeLinkClass * obj_ptr = get_mtcInv_ptr ();
    string hostname = obj_ptr->get_my_hostname();
    if ( rc != PASS )
    {
        wlog ("Failed to get an authentication token ... requesting retry\n");
        return (RETRY);
    }

    rc = mtcHttpUtil_event_init ( &obj_ptr->sysinvEvent,
                                   obj_ptr->my_hostname,
                                   "mtcInvApi_read_inventory",
                                   hostUtil_getServiceIp  (SERVICE_SYSINV),
                                   hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", hostname.c_str(), rc );
        return (rc);
    }

    /* Manage batch range */
    if (( batch == 0 ) || ( batch > MTC_INV_BATCH_MAX ))
        batch = MTC_INV_BATCH_MAX ;

    /* Add the batch integer to the request label */
    sprintf (&batch_str[0], "%d", batch );

    obj_ptr->sysinvEvent.token.url = MTC_INV_LABEL ;
    obj_ptr->sysinvEvent.token.url.append(MTC_INV_BATCH);
    obj_ptr->sysinvEvent.token.url.append(&batch_str[0]);

    obj_ptr->sysinvEvent.request     = SYSINV_GET ;
    obj_ptr->sysinvEvent.operation   = "get batch" ;
    obj_ptr->sysinvEvent.uuid        = obj_ptr->get_uuid (obj_ptr->my_hostname);
    obj_ptr->sysinvEvent.information = batch_str ;
    obj_ptr->sysinvEvent.blocking    = true      ;

    dlog ("Path:%s\n", obj_ptr->sysinvEvent.token.url.c_str());

    /* The inventory server returns a path the next element.
     * This code manages the setting the entity path that allows
     * a walk through the inventory on subsequent calls if the
     * batch number is less that the provisioned inventory number
     */

    do
    {
        /* New request, no entiry path from previous request present. */
        if (obj_ptr->sysinvEvent.entity_path.length() == 0 )
            obj_ptr->sysinvEvent.entity_path = obj_ptr->sysinvEvent.token.url.data() ;

        /* Inventory server did not specify a 'next' path meaning we are
         * at the end of inventory so start at the beginning for this
         * request
         */
        else if ( obj_ptr->sysinvEvent.entity_path_next.length() == 0 )
            obj_ptr->sysinvEvent.entity_path = obj_ptr->sysinvEvent.token.url.data() ;

        /* Get the next batch using same batch size, of inventory
         * using the inventory server's supplied 'next' entity path. */
        else
            obj_ptr->sysinvEvent.entity_path = obj_ptr->sysinvEvent.entity_path_next ;

        /* load constructed path */
        dlog ("Element Path:%s\n", obj_ptr->sysinvEvent.entity_path.c_str());

        obj_ptr->sysinvEvent.token.url = obj_ptr->sysinvEvent.entity_path ;

        /* Make the inventory request and return that result */
        obj_ptr->sysinvEvent.status = PASS ;

        do
        {
            rc = mtcHttpUtil_api_request ( obj_ptr->sysinvEvent ) ;
            if ( rc != PASS )
            {
                count++ ;
                wlog ("failed Sysinv Database Get request (%d) ... retrying (%d)\n",
                       rc , count );
            }
        } while ( ( rc != PASS ) && ( count < obj_ptr->api_retries ) ) ;

        if ( rc )
        {
            elog ("%s Sysinv Database Get Failed (%d) (cnt:%d)\n",
                      obj_ptr->sysinvEvent.entity_path.c_str(), rc , count );
            return (FAIL);
        }

        loops++ ;
    } while ( obj_ptr->sysinvEvent.entity_path_next.length());
    dlog3 ("Inventory fetched %d hosts in %d iteration using a batch of %d\n",
            obj_ptr->sysinvEvent.count, loops, batch );

    return ( obj_ptr->sysinvEvent.status );
}


/* ******************************************************************
 *
 * Name:       mtcInvApi_add_host
 *
 * Purpose:    Add a host to the database
 *
 * Note:       Presently really only used to add the first controller.
 *
 *********************************************************************/
int mtcInvApi_add_host ( node_inv_type & info )
{
    nodeLinkClass   * obj_ptr = get_mtcInv_ptr();
    string hostname = obj_ptr->get_my_hostname();
    int rc = mtcHttpUtil_event_init ( &obj_ptr->sysinvEvent,
                                      obj_ptr->my_hostname,
                                      "mtcInvApi_add_host",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", hostname.c_str(), rc );
        return (rc);
    }
    obj_ptr->sysinvEvent.inv_info    = info ;
    obj_ptr->sysinvEvent.request     = SYSINV_ADD ;
    obj_ptr->sysinvEvent.operation   = "add host" ;
    obj_ptr->sysinvEvent.blocking    = true       ;

    dlog ("%s ip:%s  mac:%s\n", info.name.c_str(),
                                info.ip.c_str(),
                                info.mac.c_str());

    return (mtcHttpUtil_api_request ( obj_ptr->sysinvEvent ));
}


/* ******************************************************************
 *
 * Name:       mtcInvApi_load_host
 *
 * Purpose:    Load all the data for a specified host from the
 *             Sysinv database
 *
 *********************************************************************/
int nodeLinkClass::mtcInvApi_load_host ( string & hostname, node_inv_type & info )
{
    string path  = ""   ;
    GET_NODE_PTR(hostname);
    int rc = mtcHttpUtil_event_init ( &node_ptr->sysinvEvent,
                                      node_ptr->hostname,
                                      "mtcInvApi_load_host",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", hostname.c_str(), rc );
        return (rc);
    }

    /* Set the host context */
    node_ptr->sysinvEvent.hostname    = hostname ;
    node_ptr->sysinvEvent.request     = SYSINV_HOST_QUERY    ; /* TODO: change to _HOST_LOAD */
    node_ptr->sysinvEvent.operation   = SYSINV_OPER__LOAD_HOST ;
    node_ptr->sysinvEvent.uuid        = node_ptr->uuid;
    node_ptr->sysinvEvent.information = hostname ;
    node_ptr->sysinvEvent.blocking    = true ;

    rc = mtcHttpUtil_api_request ( node_ptr->sysinvEvent ) ;
    if ( node_ptr->sysinvEvent.status == HTTP_NOTFOUND )
    {
        wlog ("%s not found in database\n", hostname.c_str());
    }
    else if ( rc )
    {
        wlog ("%s failed to send request to Sysinv Database (rc:%d)\n",
                  node_ptr->sysinvEvent.hostname.c_str(), rc );
    }
    else if ( node_ptr->sysinvEvent.status == PASS )
    {
       dlog ("%s found in database\n", hostname.c_str() );

       /* check for board management region mode and issue
        * a retry if its not set properly */

       info = node_ptr->sysinvEvent.inv_info ;
    }
    return (rc);
}



/*****************************************************************************
 *
 * Name    : mtcInvApi_update_task, mtcInvApi_force_task, mtcInvApi_update_task_now
 *
 * Purpose : Write the specified task and current uptime for the specified host
 *           to the inventory database.
 *
 * Note    : The 'force' version is a critical command that has retries.
 *           Failure of the force command will fail an action handler.
 *
 * Address : /v1/ihosts/63975e14-60bc-4ecd-b5c5-f2771676c0a2
 * Payload : [{"path":"/task","value":"...","op":"replace"},{"path":"/uptime","value":"123336","op":"replace"}]
 * Response: {"iports": [{"href": "http://192.168.204.2/v1/ihosts/63975e14-60bc-4ecd-b5c5-f2771676c0a2/iports" ...
 *
 *****************************************************************************/

int nodeLinkClass::mtcInvApi_update_task ( struct nodeLinkClass::node * node_ptr,
                                           const char * task_str_ptr, int one )
{
    char buffer[MAX_TASK_STR_LEN+1] ;
    snprintf ( buffer, MAX_TASK_STR_LEN, task_str_ptr, one );
    return (mtcInvApi_update_task ( node_ptr, buffer));
}

int nodeLinkClass::mtcInvApi_update_task ( struct nodeLinkClass::node * node_ptr,
                                           const char * task_str_ptr, int one, int two )
{
    char buffer[MAX_TASK_STR_LEN+1] ;
    snprintf ( buffer, MAX_TASK_STR_LEN, task_str_ptr, one, two);
    return (mtcInvApi_update_task ( node_ptr, buffer));
}

int nodeLinkClass::mtcInvApi_update_task ( struct nodeLinkClass::node * node_ptr,
                                           string task )
{
    char str [10] ;
    CHK_NODE_PTR (node_ptr);
    mtcHttpUtil_event_init ( &node_ptr->httpReq,
                              node_ptr->hostname,
                              "mtcInvApi_update_task",
                              hostUtil_getServiceIp   ( SERVICE_SYSINV ),
                              hostUtil_getServicePort ( SERVICE_SYSINV ));

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname      ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__UPDATE_TASK ;
    node_ptr->httpReq.max_retries = 0             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.noncritical = true          ;
    node_ptr->httpReq.information = task          ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_noncrit_timeout ;

    /* Store the immediate task state with the node realiing that the value
     * is not stored in the database yet but it reflective of where the code
     * execution is. */
    node_ptr->task = task ;
    node_ptr->httpReq.information = task ;

    unsigned int uptime = get_uptime ( node_ptr->hostname ) ;
    sprintf ( str , "%u", uptime );

    node_ptr->httpReq.payload = "[" ;
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_TASK);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( task );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_UPTIME);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( str );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"}]");

    if ( task.empty() )
    {
        ilog ("%s task clear (seq:%d)\n", node_ptr->hostname.c_str(), node_ptr->oper_sequence);
    }
    else
    {
        ilog ("%s Task: %s (seq:%d)\n", node_ptr->hostname.c_str(), task.c_str(), node_ptr->oper_sequence );
    }
    return(this->workQueue_enqueue( node_ptr->httpReq));
}

int nodeLinkClass::mtcInvApi_update_task ( string hostname,
                                           string task )
{
    GET_NODE_PTR(hostname); /* allocates nodeLinkClass */
    return (mtcInvApi_update_task ( node_ptr, task ));
}


int nodeLinkClass::mtcInvApi_update_task_now ( struct nodeLinkClass::node * node_ptr,
                                               string task )
{
    char str [10] ;
    CHK_NODE_PTR(node_ptr);
    mtcHttpUtil_event_init ( &this->sysinvEvent,
                              node_ptr->hostname,
                              "mtcInvApi_update_task_now",
                              hostUtil_getServiceIp   ( SERVICE_SYSINV ),
                              hostUtil_getServicePort ( SERVICE_SYSINV ));

    /* Set the host context */
    this->sysinvEvent.hostname    = node_ptr->hostname      ;
    this->sysinvEvent.uuid        = node_ptr->uuid;
    this->sysinvEvent.request     = SYSINV_UPDATE ;
    this->sysinvEvent.operation   = SYSINV_OPER__UPDATE_TASK ;
    this->sysinvEvent.max_retries = 0             ;
    this->sysinvEvent.cur_retries = 0             ;
    this->sysinvEvent.noncritical = true          ;
    this->sysinvEvent.information = task          ;
    this->sysinvEvent.timeout     = get_mtcInv_ptr()->sysinv_noncrit_timeout ;
    this->sysinvEvent.blocking = true ;

    /* Store the immediate task state with the node realiing that the value
     * is not stored in the database yet but it reflective of where the code
     * execution is. */
    node_ptr->task = task ;
    this->sysinvEvent.information = task ;

    unsigned int uptime = get_uptime ( node_ptr->hostname ) ;
    sprintf ( str , "%u", uptime );

    this->sysinvEvent.payload = "[" ;
    this->sysinvEvent.payload.append ("{\"path\":\"/") ;
    this->sysinvEvent.payload.append (MTC_JSON_INV_TASK);
    this->sysinvEvent.payload.append ("\",\"value\":\"");
    this->sysinvEvent.payload.append ( task );
    this->sysinvEvent.payload.append ( "\",\"op\":\"replace\"},");
    this->sysinvEvent.payload.append ("{\"path\":\"/") ;
    this->sysinvEvent.payload.append (MTC_JSON_INV_UPTIME);
    this->sysinvEvent.payload.append ("\",\"value\":\"");
    this->sysinvEvent.payload.append ( str );
    this->sysinvEvent.payload.append ( "\",\"op\":\"replace\"}]");

    if ( task.empty() )
    {
        ilog ("%s task clear (seq:%d)\n", node_ptr->hostname.c_str(), node_ptr->oper_sequence);
    }
    else
    {
        ilog ("%s Task: %s (seq:%d)\n", node_ptr->hostname.c_str(), task.c_str(), node_ptr->oper_sequence );
    }
    return (mtcHttpUtil_api_request ( this->sysinvEvent )) ;
}

int nodeLinkClass::mtcInvApi_update_task_now ( string hostname,
                                               string task )
{
    GET_NODE_PTR(hostname); /* allocates nodeLinkClass */
    return (mtcInvApi_update_task_now ( node_ptr, task ));
}


int nodeLinkClass::mtcInvApi_force_task ( struct nodeLinkClass::node * node_ptr,
                                          string task )
{
    char str [10] ;

    CHK_NODE_PTR(node_ptr);
    mtcHttpUtil_event_init ( &node_ptr->httpReq,
                              node_ptr->hostname,
                              "mtcInvApi_force_task",
                              hostUtil_getServiceIp   ( SERVICE_SYSINV ),
                              hostUtil_getServicePort ( SERVICE_SYSINV ));

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__FORCE_TASK ;
    node_ptr->httpReq.max_retries = 3             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.information = task          ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_timeout ;

    if ( task.empty() )
    {
        ilog ("%s task clear (seq:%d) (was:%s)\n",
                  node_ptr->hostname.c_str(),
                  node_ptr->oper_sequence,
                  node_ptr->task.empty() ? "empty" : node_ptr->task.c_str());
    }
    else
    {
        ilog ("%s Task: %s (seq:%d)\n",
                  node_ptr->hostname.c_str(),
                  task.c_str(),
                  node_ptr->oper_sequence );
    }

    /* Store the immediate task state with the node realiing that the value
     * is not stored in the database yet but it reflective of where the code
     * execution is. */
    node_ptr->task = task ;
    node_ptr->httpReq.information = task ;

    unsigned int uptime = get_uptime ( node_ptr->hostname ) ;
    sprintf ( str , "%u", uptime );

    node_ptr->httpReq.payload = "[" ;
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_TASK);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( task );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_UPTIME);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( str );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"}]");

    return(this->workQueue_enqueue( node_ptr->httpReq));
}

int nodeLinkClass::mtcInvApi_force_task ( string hostname,
                                          string task )
{
    GET_NODE_PTR(hostname); /* allocates nodeLinkClass */
    return (mtcInvApi_force_task (node_ptr, task ));
}

/*****************************************************************************
 *
 * Name    : mtcInvApi_update_value
 *
 * Purpose : Write the specified value to the specified key for the specified
 *           host to the inventory database.
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_update_value ( struct nodeLinkClass::node * node_ptr,
                                            string key,
                                            string value )
{
    CHK_NODE_PTR(node_ptr);
    int rc = mtcHttpUtil_event_init ( &node_ptr->httpReq,
                                      node_ptr->hostname,
                                      "mtcInvApi_update_value",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }

    rc = update_key_value ( node_ptr->hostname, key , value );
    if ( rc )
    {
        slog ("%s failed to update '%s' to '%s' internally (%d)\n",
                  node_ptr->hostname.c_str(),
                  key.c_str(),
                  value.c_str(),
                  rc );

        return (rc);
    }
    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname      ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__UPDATE_VALUE ;
    node_ptr->httpReq.key         = key           ;
    node_ptr->httpReq.value       = value         ;
    node_ptr->httpReq.max_retries = 3             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_timeout ;

    node_ptr->httpReq.information = key           ;
    node_ptr->httpReq.information.append(":")     ;
    node_ptr->httpReq.information.append(value)   ;

    node_ptr->httpReq.payload = "[" ;
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (key.data());
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append (value.data());
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"}]");

    return(this->workQueue_enqueue( node_ptr->httpReq));
}

int nodeLinkClass::mtcInvApi_update_value ( string hostname,
                                            string key,
                                            string value )
{
    GET_NODE_PTR(hostname);
    return (mtcInvApi_update_value ( node_ptr, key, value ));
}

/*****************************************************************************
 *
 * Name    : mtcInvApi_update_uptime
 *
 * Purpose : Write a hosts uptime to the inventory database.
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_update_uptime ( struct nodeLinkClass::node * node_ptr,
                                             unsigned int uptime )
{
    char str [10] ;
    CHK_NODE_PTR(node_ptr);
    int rc = mtcHttpUtil_event_init ( &node_ptr->httpReq,
                                      node_ptr->hostname,
                                      "mtcInvApi_update_uptime",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname      ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__UPDATE_UPTIME ;
    node_ptr->httpReq.max_retries = 0             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.noncritical = true          ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_noncrit_timeout ;

    sprintf ( str , "%d", uptime );

    /* TODO: remove me, the str should be fine */
    string uptime_str = str ;

    node_ptr->httpReq.information = uptime_str    ;

    /* Sent uptime update request.
     * But exit this iteration if we get an error as we
     * don't want to stall mtce for all hosts on such a
     * simple operation */

    node_ptr->httpReq.payload = "[" ;
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_UPTIME);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( uptime_str );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"}]");
    rc = this->workQueue_enqueue  ( node_ptr->httpReq);
    set_uptime_refresh_ctr ( node_ptr->hostname, 0 ) ;
    return (rc) ;
}

int nodeLinkClass::mtcInvApi_update_uptime ( string       hostname,
                                             unsigned int uptime )
{
    GET_NODE_PTR(hostname);
    return (mtcInvApi_update_uptime ( node_ptr, uptime ));
}

/*****************************************************************************
 *
 * Name    : mtcInvApi_force_states
 *
 * Purpose : Write all 3 maintenance states to the inv database.
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_force_states ( struct nodeLinkClass::node * node_ptr,
                                            string admin,
                                            string oper,
                                            string avail )
{
    CHK_NODE_PTR(node_ptr);
    int rc = mtcHttpUtil_event_init ( &node_ptr->httpReq,
                                      node_ptr->hostname,
                                      "mtcInvApi_force_states",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__FORCE_STATES ;
    node_ptr->httpReq.max_retries = 3             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_timeout ;

    node_ptr->httpReq.information = admin         ;
    node_ptr->httpReq.information.append("-")     ;
    node_ptr->httpReq.information.append(oper)    ;
    node_ptr->httpReq.information.append("-")     ;
    node_ptr->httpReq.information.append(avail)   ;

    node_ptr->httpReq.payload = "[" ;
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_ADMIN);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( admin );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");

    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_OPER);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( oper );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");

    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_AVAIL);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( avail );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"}]");

    rc = this->workQueue_enqueue ( node_ptr->httpReq);
    return (rc);
}

int nodeLinkClass::mtcInvApi_force_states ( string hostname,
                                            string admin,
                                            string oper,
                                            string avail )
{
    GET_NODE_PTR(hostname); /* allocates nodeLinkClass */
    return (mtcInvApi_force_states (node_ptr, admin, oper, avail ));

}

/*****************************************************************************
 *
 * Name    : mtcInvApi_subf_states
 *
 * Purpose : Write all 5 maintenance states to the inv database.
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_subf_states ( struct nodeLinkClass::node * node_ptr,
                                           string oper_subf,
                                           string avail_subf )
{
    CHK_NODE_PTR(node_ptr);
    int rc = mtcHttpUtil_event_init ( &node_ptr->httpReq,
                                      node_ptr->hostname,
                                      "mtcInvApi_subf_states",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }

    oper_subf_state_change   ( node_ptr->hostname, oper_subf  );
    avail_subf_status_change ( node_ptr->hostname, avail_subf );

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname      ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__FORCE_STATES ;
    node_ptr->httpReq.max_retries = 3             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_timeout ;

    node_ptr->httpReq.information = oper_subf     ;
    node_ptr->httpReq.information.append("-")     ;
    node_ptr->httpReq.information.append(avail_subf);

    node_ptr->httpReq.payload = "[" ;
    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_OPER_SUBF);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( oper_subf );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");

    node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
    node_ptr->httpReq.payload.append (MTC_JSON_INV_AVAIL_SUBF);
    node_ptr->httpReq.payload.append ("\",\"value\":\"");
    node_ptr->httpReq.payload.append ( avail_subf );
    node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"}]");

    rc = this->workQueue_enqueue ( node_ptr->httpReq);
    return (rc);
}

int nodeLinkClass::mtcInvApi_subf_states ( string hostname,
                                           string oper_subf,
                                           string avail_subf )
{
    GET_NODE_PTR(hostname);
    return ( mtcInvApi_subf_states ( node_ptr, oper_subf, avail_subf ));
}

/*****************************************************************************
 *
 * Name    : mtcInvApi_update_states
 *
 * Purpose : Write new values for the admin, oper and avail states to the
 *           inventory database.
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_update_states ( struct nodeLinkClass::node * node_ptr,
                                             string admin,
                                             string oper,
                                             string avail )
{
    int changes = 0    ;
    CHK_NODE_PTR(node_ptr);
    int rc = mtcHttpUtil_event_init ( &node_ptr->httpReq,
                                      node_ptr->hostname,
                                      "mtcInvApi_update_states",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname      ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__UPDATE_STATES ;
    node_ptr->httpReq.max_retries = 3             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_timeout ;

    node_ptr->httpReq.payload = "[" ;
    if ( ! admin.empty() )
    {
        admin_state_change  ( node_ptr->hostname, admin );
        node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
        node_ptr->httpReq.payload.append (MTC_JSON_INV_ADMIN);
        node_ptr->httpReq.payload.append ("\",\"value\":\"");
        node_ptr->httpReq.payload.append ( admin );
        node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");
        changes++ ;
    }
    else
        admin = " " ;

    if ( ! oper.empty() )
    {
        oper_state_change ( node_ptr->hostname, oper  );
        node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
        node_ptr->httpReq.payload.append (MTC_JSON_INV_OPER);
        node_ptr->httpReq.payload.append ("\",\"value\":\"");
        node_ptr->httpReq.payload.append ( oper );
        node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");
        changes++ ;
    }
    else
        oper = " " ;

    if ( ! avail.empty() )
    {
        avail_status_change ( node_ptr->hostname, avail );
        node_ptr->httpReq.payload.append ("{\"path\":\"/") ;
        node_ptr->httpReq.payload.append (MTC_JSON_INV_AVAIL);
        node_ptr->httpReq.payload.append ("\",\"value\":\"");
        node_ptr->httpReq.payload.append ( avail );
        node_ptr->httpReq.payload.append ( "\",\"op\":\"replace\"},");
        changes++ ;
    }
    else
        avail = " " ;

    if ( changes )
    {
        node_ptr->httpReq.information = admin         ;
        node_ptr->httpReq.information.append("-")     ;
        node_ptr->httpReq.information.append(oper)    ;
        node_ptr->httpReq.information.append("-")     ;
        node_ptr->httpReq.information.append(avail)   ;

        /* remove the last "," and complete the string */
        int len = node_ptr->httpReq.payload.length();
        node_ptr->httpReq.payload.erase(len-1,1);
        node_ptr->httpReq.payload.append ( "]");

        ilog ("%s %s-%s-%s (seq:%d)\n", node_ptr->hostname.c_str(),
                               admin.c_str(),
                               oper.c_str(),
                               avail.c_str(),
                               node_ptr->oper_sequence);

        rc = this->workQueue_enqueue ( node_ptr->httpReq);
    }
    else
    {
        dlog ("%s -> %s-%s-%s\n", node_ptr->hostname.c_str(),
                                  admin.c_str(),
                                   oper.c_str(),
                                  avail.c_str());
    }
    return (rc);
}

int nodeLinkClass::mtcInvApi_update_states ( string hostname,
                                             string admin,
                                             string oper,
                                             string avail )
{
    GET_NODE_PTR(hostname);
    return (mtcInvApi_update_states ( node_ptr, admin, oper, avail ));
}

/*****************************************************************************
 *
 * Name    : mtcInvApi_update_states_now
 *
 * Purpose : Write new values for the admin, oper and avail states to the
 *           inventory database.
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_update_states_now ( struct nodeLinkClass::node * node_ptr,
                                                 string admin,
                                                 string oper,
                                                 string avail,
                                                 string oper_subf,
                                                 string avail_subf)
{
    int changes = 0    ;
    CHK_NODE_PTR(node_ptr);
    int rc = mtcHttpUtil_event_init ( &this->sysinvEvent,
                                      node_ptr->hostname,
                                      "mtcInvApi_update_states_now",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }

    /* Set the host context */
    this->sysinvEvent.hostname    = node_ptr->hostname ;
    this->sysinvEvent.uuid        = node_ptr->uuid;
    this->sysinvEvent.request     = SYSINV_UPDATE ;
    this->sysinvEvent.operation   = SYSINV_OPER__UPDATE_STATES ;
    this->sysinvEvent.max_retries = 3             ;
    this->sysinvEvent.cur_retries = 0             ;
    this->sysinvEvent.timeout     = get_mtcInv_ptr()->sysinv_timeout ;

    this->sysinvEvent.payload = "[" ;
    if ( ! admin.empty() )
    {
        admin_state_change  ( node_ptr->hostname, admin );
        this->sysinvEvent.payload.append ("{\"path\":\"/") ;
        this->sysinvEvent.payload.append (MTC_JSON_INV_ADMIN);
        this->sysinvEvent.payload.append ("\",\"value\":\"");
        this->sysinvEvent.payload.append ( admin );
        this->sysinvEvent.payload.append ( "\",\"op\":\"replace\"},");
        changes++ ;
    }
    else
        admin = " " ;

    if ( ! oper.empty() )
    {
        oper_state_change ( node_ptr->hostname, oper  );
        this->sysinvEvent.payload.append ("{\"path\":\"/") ;
        this->sysinvEvent.payload.append (MTC_JSON_INV_OPER);
        this->sysinvEvent.payload.append ("\",\"value\":\"");
        this->sysinvEvent.payload.append ( oper );
        this->sysinvEvent.payload.append ( "\",\"op\":\"replace\"},");
        changes++ ;
    }
    else
        oper = " " ;

    if ( ! avail.empty() )
    {
        avail_status_change ( node_ptr->hostname, avail );
        this->sysinvEvent.payload.append ("{\"path\":\"/") ;
        this->sysinvEvent.payload.append (MTC_JSON_INV_AVAIL);
        this->sysinvEvent.payload.append ("\",\"value\":\"");
        this->sysinvEvent.payload.append ( avail );
        this->sysinvEvent.payload.append ( "\",\"op\":\"replace\"},");
        changes++ ;
    }
    else
        avail = " " ;

    if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
    {
        if ( ! oper_subf.empty() )
        {
            oper_subf_state_change ( node_ptr->hostname, oper_subf  );
            this->sysinvEvent.payload.append ("{\"path\":\"/") ;
            this->sysinvEvent.payload.append (MTC_JSON_INV_OPER_SUBF);
            this->sysinvEvent.payload.append ("\",\"value\":\"");
            this->sysinvEvent.payload.append ( oper_subf );
            this->sysinvEvent.payload.append ( "\",\"op\":\"replace\"},");
            changes++ ;
        }
        else
            oper_subf = " " ;

        if ( ! avail_subf.empty() )
        {
            avail_subf_status_change ( node_ptr->hostname, avail_subf );
            this->sysinvEvent.payload.append ("{\"path\":\"/") ;
            this->sysinvEvent.payload.append (MTC_JSON_INV_AVAIL_SUBF);
            this->sysinvEvent.payload.append ("\",\"value\":\"");
            this->sysinvEvent.payload.append ( avail_subf );
            this->sysinvEvent.payload.append ( "\",\"op\":\"replace\"},");
            changes++ ;
        }
        else
            avail_subf = "" ;
    }

    if ( changes )
    {
        this->sysinvEvent.information = admin         ;
        this->sysinvEvent.information.append("-")     ;
        this->sysinvEvent.information.append(oper)    ;
        this->sysinvEvent.information.append("-")     ;
        this->sysinvEvent.information.append(avail)   ;

        /* remove the last "," and complete the string */
        int len = this->sysinvEvent.payload.length();
        this->sysinvEvent.payload.erase(len-1,1);
        this->sysinvEvent.payload.append ( "]");

        if (( CPE_SYSTEM ) && ( is_controller(node_ptr) == true ))
        {
            ilog ("%s %s-%s-%s %s-%s\n",
                      node_ptr->hostname.c_str(),
                      admin.c_str(),
                      oper.c_str(),
                      avail.c_str(),
                      oper_subf.c_str(),
                      avail_subf.c_str());
        }
        else
        {
            ilog ("%s %s-%s-%s\n",
                      node_ptr->hostname.c_str(),
                      admin.c_str(),
                      oper.c_str(),
                      avail.c_str());
        }

        this->sysinvEvent.blocking = true ;
        rc = mtcHttpUtil_api_request ( this->sysinvEvent ) ;
    }
    else
    {
        dlog ("%s -> %s-%s-%s\n", node_ptr->hostname.c_str(),
                                  admin.c_str(),
                                   oper.c_str(),
                                  avail.c_str());
    }
    return (rc);
}

int nodeLinkClass::mtcInvApi_update_states_now ( string hostname,
                                                 string admin,
                                                 string oper,
                                                 string avail,
                                                 string oper_subf,
                                                 string avail_subf)
{
    GET_NODE_PTR(hostname);
    return ( mtcInvApi_update_states_now ( node_ptr, admin, oper, avail, oper_subf, avail_subf ));
}

/*****************************************************************************
 *
 * Name    : mtcInvApi_update_state
 *
 * Purpose : Write a new mtce state value to the inventory database.
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_update_state ( struct nodeLinkClass::node * node_ptr,
                                            string state,
                                            string value )
{
    CHK_NODE_PTR(node_ptr);
    int rc = mtcHttpUtil_event_init ( &node_ptr->httpReq,
                                      node_ptr->hostname,
                                      "mtcInvApi_update_state",
                                      hostUtil_getServiceIp  (SERVICE_SYSINV),
                                      hostUtil_getServicePort(SERVICE_SYSINV));
    if ( rc )
    {
        elog ("%s failed to allocate libEvent memory (%d)\n", node_ptr->hostname.c_str(), rc );
        return (rc);
    }

    if ( !state.compare(MTC_JSON_INV_ADMIN))
        admin_state_change  ( node_ptr->hostname, value );
    else if ( !state.compare(MTC_JSON_INV_OPER))
        oper_state_change   ( node_ptr->hostname, value  );
    else if ( !state.compare(MTC_JSON_INV_AVAIL))
        avail_status_change   ( node_ptr->hostname, value );

    else if ( !state.compare(MTC_JSON_INV_OPER_SUBF))
        oper_subf_state_change   ( node_ptr->hostname, value );
    else if ( !state.compare(MTC_JSON_INV_AVAIL_SUBF))
        avail_subf_status_change   ( node_ptr->hostname, value );

    /* Set the host context */
    node_ptr->httpReq.hostname    = node_ptr->hostname ;
    node_ptr->httpReq.uuid        = node_ptr->uuid;
    node_ptr->httpReq.request     = SYSINV_UPDATE ;
    node_ptr->httpReq.operation   = SYSINV_OPER__UPDATE_STATE ;
    node_ptr->httpReq.key         = state         ;
    node_ptr->httpReq.value       = value         ;
    node_ptr->httpReq.max_retries = 3             ;
    node_ptr->httpReq.cur_retries = 0             ;
    node_ptr->httpReq.timeout     = get_mtcInv_ptr()->sysinv_timeout ;

    node_ptr->httpReq.information = state         ;
    node_ptr->httpReq.information.append(":")     ;
    node_ptr->httpReq.information.append(value)   ;

    if (( !state.compare(MTC_JSON_INV_AVAIL_SUBF) )||
        ( !state.compare(MTC_JSON_INV_OPER_SUBF) ))
    {
        ilog ("%s-compute %s (seq:%d)\n", node_ptr->hostname.c_str(), value.c_str(), node_ptr->oper_sequence);
    }
    else
    {
        ilog ("%s %s (seq:%d)\n", node_ptr->hostname.c_str(), value.c_str(), node_ptr->oper_sequence);
    }

    node_ptr->httpReq.payload = "[{\"path\":\"/" ;
    node_ptr->httpReq.payload.append ( state );
    node_ptr->httpReq.payload.append ( "\", \"value\": \"");
    node_ptr->httpReq.payload.append ( value );
    node_ptr->httpReq.payload.append ( "\", \"op\": \"replace\"}]");
    return (this->workQueue_enqueue( node_ptr->httpReq));
}

int nodeLinkClass::mtcInvApi_update_state ( string hostname,
                                            string state,
                                            string value )
{
    GET_NODE_PTR(hostname);
    return (mtcInvApi_update_state ( node_ptr, state, value ));
}


/*****************************************************************************
 *
 * Name    : mtcInvApi_cfg_show
 *
 * Purpose : Issue a configuration show command to sysinv
 *
 * Type    : GET
 * Address : /v1/iuser
 * Payload : none
 * Response: {"iusers":
 *              [
 *                {"recordtype": "reconfig", "links":
 *                   [
 *                      {"href": "http://192.168.204.2:6385/v1/iusers/286a0793-5d15-473d-a459-00c2bfc369cb", "rel": "self"},
 *                      {"href": "http://192.168.204.2:6385/iusers/286a0793-5d15-473d-a459-00c2bfc369cb", "rel": "bookmark"}
 *                   ]
 *                 "created_at"  : "2014-09-30T14:42:16.704390+00:00",
 *                 "updated_at"  : "2014-09-30T20:41:07.250815+00:00",
 *                 "root_sig"    : "60550974db5458fab1fbb5ccf4f18c4d",
 *                 "istate"      : "applied",
 *                 "isystem_uuid": "ce178041-2b2c-405d-bf87-f19334a35582",
 *                 "uuid"        : "286a0793-5d15-473d-a459-00c2bfc369cb"
 *                 }
 *              ]
 *           }
 *
 *****************************************************************************/
int nodeLinkClass::mtcInvApi_cfg_show ( string hostname )
{
    GET_NODE_PTR(hostname); /* allocates nodeLinkClass */
    mtcHttpUtil_event_init ( &node_ptr->cfgEvent,
                              hostname,
                              "mtcInvApi_cfg_show",
                              hostUtil_getServiceIp   ( SERVICE_SYSINV ),
                              hostUtil_getServicePort ( SERVICE_SYSINV ));

    /* Set the host context */
    node_ptr->cfgEvent.hostname    = hostname      ;
    node_ptr->cfgEvent.uuid        = node_ptr->uuid;
    node_ptr->cfgEvent.status      = PASS          ;
    node_ptr->cfgEvent.request     = SYSINV_CONFIG_SHOW ;
    node_ptr->cfgEvent.operation   = SYSINV_OPER__CONFIG_SHOW ;
    node_ptr->cfgEvent.max_retries = 3             ;
    node_ptr->cfgEvent.cur_retries = 0             ;

    return(this->workQueue_enqueue ( node_ptr->cfgEvent));
}

/*****************************************************************************
 *
 * Name    : mtcInvApi_cfg_modify
 *
 * Purpose : Issue a configuration modify command to sysinv
 *
 * Description:
 *
 * The initial modify is done with an install command if install boolean
 * is true otherwise the apply command is sent.
 *
 * event.key    holds the new password line signature from the shadow file
 * event.value  holds the current password line signature in the database
 * event.uuid   holds the reconfig uuid to be used in the url for the command
 * event.information holds the current password hash and password age from the shadow file
 *
 * Type    : GET
 * Address : /v1/iuser
 * Payload : none
 * Response: {"iusers":
 *              [
 *                {"recordtype": "reconfig", "links":
 *                   [
 *                      {"href": "http://192.168.204.2:6385/v1/iusers/286a0793-5d15-473d-a459-00c2bfc369cb", "rel": "self"},
 *                      {"href": "http://192.168.204.2:6385/iusers/286a0793-5d15-473d-a459-00c2bfc369cb", "rel": "bookmark"}
 *                   ]
 *                 "created_at"  : "2014-09-30T14:42:16.704390+00:00",
 *                 "updated_at"  : "2014-09-30T20:41:07.250815+00:00",
 *                 "root_sig"    : "60550974db5458fab1fbb5ccf4f18c4d",
 *                 "passwd_expiry_days": "45",
 *                 "passwd_hash" : "DkGo4WZdJqemnDgX26oJlZTp8cj61",
 *                 "istate"      : "applied",
 *                 "isystem_uuid": "ce178041-2b2c-405d-bf87-f19334a35582",
 *                 "uuid"        : "286a0793-5d15-473d-a459-00c2bfc369cb"
 *                 }
 *              ]
 *           }
 *
 *****************************************************************************/
int  nodeLinkClass::mtcInvApi_cfg_modify ( string hostname, bool install )
{
    GET_NODE_PTR(hostname); /* allocates nodeLinkClass */

    // stow away the information since it will get cleared
    // as part of mtcHttpUtil_event_init
    string cfgInfo = node_ptr->cfgEvent.information;

    mtcHttpUtil_event_init ( &node_ptr->cfgEvent,
                              hostname,
                              "mtcInvApi_cfg_modify",
                              hostUtil_getServiceIp   ( SERVICE_SYSINV ),
                              hostUtil_getServicePort ( SERVICE_SYSINV ));

    /* Set the host context */
    node_ptr->cfgEvent.hostname    = hostname      ;
    node_ptr->cfgEvent.request     = SYSINV_CONFIG_MODIFY ;
    node_ptr->cfgEvent.operation   = SYSINV_OPER__CONFIG_MODIFY ;
    node_ptr->cfgEvent.max_retries = 3             ;
    node_ptr->cfgEvent.cur_retries = 0             ;
    node_ptr->cfgEvent.rx_retry_max= get_mtcInv_ptr()->sysinv_timeout * 1000;

    /* Get the invCfg hash and age fields separated by ':' */
    char cfgHash[1024], cfgAging[1024];
    sscanf(cfgInfo.c_str(), "%1023[^:]:%1023[^:]", cfgHash, cfgAging);
           /* "%[^:]:%[^:]", cfgHash, cfgAging);
            *
            * "%1023[^:]:%1023[^:]", cfgHash, cfgAging);
            * Eventually replace with this line after testing.
            */
    node_ptr->cfgEvent.payload = "[";
    node_ptr->cfgEvent.payload.append ( "{\"path\":\"/root_sig\"," );
    node_ptr->cfgEvent.payload.append ( "\"value\":\"" );
    node_ptr->cfgEvent.payload.append ( node_ptr->cfgEvent.key );
    node_ptr->cfgEvent.payload.append ("\",\"op\":\"replace\"},");

    node_ptr->cfgEvent.payload.append ( "{\"path\":\"/passwd_expiry_days\"," );
    node_ptr->cfgEvent.payload.append ( "\"value\":\"" );
    node_ptr->cfgEvent.payload.append ( cfgAging );
    node_ptr->cfgEvent.payload.append ("\",\"op\":\"replace\"},");

    node_ptr->cfgEvent.payload.append ( "{\"path\":\"/passwd_hash\"," );
    node_ptr->cfgEvent.payload.append ( "\"value\":\"" );
    node_ptr->cfgEvent.payload.append ( cfgHash );
    node_ptr->cfgEvent.payload.append ("\",\"op\":\"replace\"},");

    node_ptr->cfgEvent.payload.append ( "{\"path\":\"/action\",");

    if ( install )
        node_ptr->cfgEvent.payload.append ( "\"value\":\"install\",");
    else
        node_ptr->cfgEvent.payload.append ( "\"value\":\"apply\",");

    node_ptr->cfgEvent.payload.append ( "\"op\":\"replace\"}]");

    qlog ("%s Payload: %s\n", hostname.c_str(),
          node_ptr->cfgEvent.payload.c_str() );

    return (this->workQueue_enqueue( node_ptr->cfgEvent));
}


/*****************************************************************************/
/********************        H A N D L E R S          ************************/
/*****************************************************************************/

int mtcInvApi_handler ( libEvent & event )
{
    int rc = FAIL ;
    char * resp_ptr = (char*)event.response.data();

    if (( !event.operation.compare(SYSINV_OPER__UPDATE_TASK)) ||
        ( !event.operation.compare(SYSINV_OPER__FORCE_TASK)))
    {
        /* Load the inventory response */
        rc = jsonUtil_patch_load ( resp_ptr, event.inv_info);
    }
    else if ( !event.operation.compare(SYSINV_OPER__UPDATE_UPTIME))
    {
        rc = jsonUtil_patch_load ( resp_ptr, event.inv_info);
    }
    else if ( !event.operation.compare(SYSINV_OPER__UPDATE_VALUE))
    {
        /* Load the inventory response */
        rc = jsonUtil_patch_load ( resp_ptr, event.inv_info);
        if ( rc != PASS )
        {
            elog ("Bad inventory response to update key:value '%s:%s' request\n",
                   event.key.c_str(), event.value.c_str());
        }
#ifdef WANT_KEY_VALUE_FROM_DATABASE
        else
        {
            nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
            rc = obj_ptr->update_key_value ( event.hostname, event.key , event.value );
        }
#endif
    }
    else if (( !event.operation.compare(SYSINV_OPER__UPDATE_STATE)) ||
             ( !event.operation.compare(SYSINV_OPER__UPDATE_STATES)) ||
             ( !event.operation.compare(SYSINV_OPER__FORCE_STATES)))
    {
        /* Load the inventory response */
        rc = jsonUtil_patch_load ( resp_ptr, event.inv_info);
        if ( rc != PASS )
        {
            elog ("Unable to communicate with inventory database\n");
                   event.status = FAIL_DATABASE_DOWN ;
        }
        else
        {
            if ( (!event.inv_info.name.compare("none")) ||
                  (event.inv_info.name.empty()))
            {
                elog ("Got a None or Null Response from Inventory 'Update' Request\n");
                print_inv ( event.inv_info );
                event.status = FAIL_STRING_EMPTY ;
            }
            else
            {
                if (!adminStateOk (event.inv_info.admin))
                {
                    event.status = FAIL_INVALID_DATA ;
                    elog ("%s missing or invalid 'admin' state from Inv Patch response\n",
                              event.inv_info.name.c_str());
                }
                if (!operStateOk (event.inv_info.oper))
                {
                    event.status = FAIL_INVALID_DATA ;
                    elog ("%s missing or invalid 'oper' state from Inv Patch response\n",
                              event.inv_info.name.c_str());
                }
                if (!availStatusOk (event.inv_info.avail))
                {
                    event.status = FAIL_INVALID_DATA ;
                    elog ("%s missing or invalid 'avail' status from Inv Patch response\n",
                              event.inv_info.name.c_str());
                }
            }
        }
    }
    else if (( !event.operation.compare(SYSINV_OPER__CONFIG_SHOW  )) ||
             ( !event.operation.compare(SYSINV_OPER__CONFIG_MODIFY)))
    {
        ilog ("%s Handled (in FSM)\n", event.log_prefix.c_str());
        if ( event.response.length() > 200 )
        {
            rc = PASS ;
        }
        else
        {
            rc = FAIL_INVALID_DATA ;
        }
    }

    if ( rc )
    {
       wlog ("%s Handled with error (%d:%d)\n", event.log_prefix.c_str(), event.status, rc );
       return (rc);
    }

    else if ( event.status )
        return (event.status);

    return ( PASS );
}

/* The handles the Inventory Add (POST) request's response */
void nodeLinkClass::mtcInvApi_add_handler ( struct evhttp_request *req, void *arg )
{
    int rc = PASS ;

    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    if ( !req )
    {
        elog ("%s '%s' Request Timeout (%d) (%p)\n",
                  sysinvEvent.hostname.c_str(),
                  sysinvEvent.service.c_str(),
                  sysinvEvent.timeout , arg);

        sysinvEvent.status = FAIL_TIMEOUT ;
        goto _add_handler_done ;
    }

    obj_ptr->sysinvEvent.status = mtcHttpUtil_status ( obj_ptr->sysinvEvent ) ;
    if ( obj_ptr->sysinvEvent.status != PASS )
    {
        elog ("%s Sysinv Add Failed (%d)\n",
                  obj_ptr->sysinvEvent.hostname.c_str(),
                  obj_ptr->sysinvEvent.status );
        goto _add_handler_done ;
    }

    if ( mtcHttpUtil_get_response ( obj_ptr->sysinvEvent ) != PASS )
        goto _add_handler_done ;

    /* Parse through the response and fill in json_info */
    rc = jsonUtil_load_host ( (char*)obj_ptr->sysinvEvent.response.data(),
                                 obj_ptr->sysinvEvent.inv_info );
    if ( rc != PASS )
    {
        elog ("%s Failed to Parse Sysinv Response (json)\n",
                  obj_ptr->sysinvEvent.hostname.c_str());

        obj_ptr->sysinvEvent.status = FAIL_JSON_PARSE ;
    }

    jlog ("%s Response: %s\n", obj_ptr->sysinvEvent.hostname.c_str(),
                               obj_ptr->sysinvEvent.response.c_str());


_add_handler_done:

    event_base_loopbreak((struct event_base *)arg);
}

/* The handles the inventory Query (QUERY) request's response */
void nodeLinkClass::mtcInvApi_get_handler ( struct evhttp_request *req, void *arg )
{
    int rc = PASS ;

    /* Declare and clean the json info object string containers */
    jsonUtil_info_type json_info ;
    jsonUtil_init ( json_info ); /* init it */

    /* Find the host this handler instance is being run against
     * and get its event base - sysinvEvent.base */
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    if ( ! req )
    {
        elog ("Inventory 'Get' Request Timeout (%s)\n",
                  obj_ptr->sysinvEvent.entity_path.c_str());

        obj_ptr->sysinvEvent.status = FAIL_TIMEOUT ;
        goto _get_handler_done ;
    }

    /* Check the HTTP Status Code */
    else if ( mtcHttpUtil_status ( obj_ptr->sysinvEvent ) != PASS )
    {
        elog ("Inventory HTTP Get Request Failed (%s) (%d)\n",
                  obj_ptr->sysinvEvent.entity_path.c_str(),
                  obj_ptr->sysinvEvent.status );
        goto _get_handler_done ;
    }

    if ( mtcHttpUtil_get_response ( obj_ptr->sysinvEvent ) != PASS )
    {
        elog ("Inventory Server may be down (%s)\n",
               obj_ptr->sysinvEvent.entity_path.c_str());
        json_info.elements = 0 ;
    }
    else
    {
        /* Parse through the response and fill in json_info */
        rc = jsonUtil_inv_load ( (char*)obj_ptr->sysinvEvent.response.data(), json_info );
        if ( rc == FAIL )
        {
           elog ("Unable to parse inventory response (%s) (%s)\n",
                     obj_ptr->sysinvEvent.entity_path.c_str(),
                     obj_ptr->sysinvEvent.response.c_str());
           json_info.elements = 0 ;
        }
        else
        {
           dlog ("%s json string with %d records\n", obj_ptr->sysinvEvent.entity_path.c_str(),
                  json_info.elements );
           obj_ptr->sysinvEvent.entity_path_next = json_info.next.c_str();
        }
        for ( int i = 0 ; i < json_info.elements ; i++ )
        {
            if ( json_info.host[i].uuid.length() == UUID_LEN )
            {
                nodeLinkClass * obj_ptr = get_mtcInv_ptr ();
                node_inv_type node ;
                node.uuid  = json_info.host[i].uuid ;
                node.name  = json_info.host[i].name ;
                node.ip    = json_info.host[i].ip   ;
                node.mac   = json_info.host[i].mac  ;
                node.id    = json_info.host[i].id   ;
                node.admin = json_info.host[i].admin;
                node.oper  = json_info.host[i].oper ;
                node.avail = json_info.host[i].avail;
                node.type  = json_info.host[i].type ;
                node.func  = json_info.host[i].func ;
                node.task  = json_info.host[i].task ;

                node.uptime= json_info.host[i].uptime ;

                node.bm_un    = json_info.host[i].bm_un ;
                node.bm_ip    = json_info.host[i].bm_ip ;
                node.bm_type  = json_info.host[i].bm_type ;

                node.oper_subf  = json_info.host[i].oper_subf ;
                node.avail_subf = json_info.host[i].avail_subf ;
                node.infra_ip    = json_info.host[i].infra_ip ;

                if (node.name.compare("none"))
                {
                    obj_ptr->sysinvEvent.count++ ;

                    /* Add the node to maintenance */

                    rc = obj_ptr->add_host ( node ) ;
                    switch (rc)
                    {
                        case RETRY:
                            break ;
                        case PASS:
                            if ( obj_ptr->get_operState (node.name) == MTC_OPER_STATE__ENABLED )
                            {
                                obj_ptr->ctl_mtcAlive_gate (node.name, false) ;
                            }
                            // jsonUtil_print ( json_info, i );
                            break ;
                        case FAIL:
                        default:
                            elog ("Failed to add hostname '%s' to maintenance (rc:%d)\n",
                                 node.name.c_str(), rc );
                            break ;
                    }
                }
                else
                {
                    dlog ("Refusing to add hostname '%s' to maintenance\n", node.name.c_str());
                }
            }
        }
    }

_get_handler_done:

    /* This is needed to get out of the loop */
    event_base_loopbreak((struct event_base *)arg);
}


/* The handles the Inventory Query request's response
 * Should only be called for the active controller */
void nodeLinkClass::mtcInvApi_qry_handler ( struct evhttp_request *req, void *arg )
{
    int rc = PASS ;

    /* Find the host this handler instance is being run against
     * and get its event base - sysinvEvent.base */
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    nodeLinkClass::node * node_ptr =
    obj_ptr->getEventBaseNode ( SYSINV_HOST_QUERY, (struct event_base *)arg ) ;

    if ( node_ptr == NULL )
    {
        slog ("Node Lookup Failed - Sysinv Event (%p)\n", arg);
        goto _qry_handler_done ;
    }

    else if ( node_ptr->sysinvEvent.hostname.empty() )
    {
        elog ("%s Don't know what hostname to look for\n",
                  node_ptr->sysinvEvent.hostname.c_str());
        node_ptr->sysinvEvent.status = FAIL_UNKNOWN_HOSTNAME ;
        goto _qry_handler_done ;
    }

    else if ( ! req )
    {
        elog ("%s 'Query' Request Timeout (%d)\n",
                  node_ptr->sysinvEvent.hostname.c_str(),
                  node_ptr->sysinvEvent.timeout);

        node_ptr->sysinvEvent.status = FAIL_TIMEOUT ;
        goto _qry_handler_done ;
    }

    /* Check the HTTP Status Code */
    node_ptr->sysinvEvent.status = mtcHttpUtil_status ( node_ptr->sysinvEvent ) ;
    if ( node_ptr->sysinvEvent.status == HTTP_NOTFOUND )
    {
        dlog ("%s Sysinv Query (Not-Found) (%d)\n",
                  node_ptr->hostname.c_str(),
                  node_ptr->sysinvEvent.status);

        goto _qry_handler_done ;
    }
    else if ( node_ptr->sysinvEvent.status != PASS )
    {
        elog ("%s Sysinv HTTP Request Failed (%d)\n",
                  node_ptr->hostname.c_str(),
                  node_ptr->sysinvEvent.status);

        goto _qry_handler_done ;
    }

    if ( mtcHttpUtil_get_response ( node_ptr->sysinvEvent ) != PASS )
        goto _qry_handler_done ;

    jlog ("%s Address : %s\n", node_ptr->sysinvEvent.hostname.c_str(),
                               node_ptr->sysinvEvent.address.c_str());
    jlog ("%s Payload : %s\n", node_ptr->sysinvEvent.hostname.c_str(),
                               node_ptr->sysinvEvent.payload.c_str());
    jlog ("%s Response: %s\n", node_ptr->sysinvEvent.hostname.c_str(),
                               node_ptr->sysinvEvent.response.c_str());

    node_inv_init ( node_ptr->sysinvEvent.inv_info );
    rc = jsonUtil_load_host ( (char*)node_ptr->sysinvEvent.response.data(),
                                        node_ptr->sysinvEvent.inv_info  );
    if ( rc != PASS )
    {
        elog ("%s Failed to Parse Json Response\n", node_ptr->sysinvEvent.hostname.c_str());
        node_ptr->sysinvEvent.status = FAIL_JSON_PARSE ;
    }
    else
    {
        /* Pass the operaiton regardless of finding it */
        node_ptr->sysinvEvent.status = HTTP_NOTFOUND ;
        if ( node_ptr->sysinvEvent.inv_info.uuid.length() == UUID_LEN )
        {
            if ( !node_ptr->sysinvEvent.inv_info.name.compare(node_ptr->sysinvEvent.hostname))
            {
                node_ptr->sysinvEvent.status = PASS ;
            }
            else
            {
                wlog ("%s was not found in the database\n",
                          node_ptr->sysinvEvent.hostname.c_str());
                node_ptr->sysinvEvent.status = HTTP_NOTFOUND ;
            }
        }
        else
        {
            wlog ("%s has no uuid\n", node_ptr->sysinvEvent.hostname.c_str());
        }
    }

_qry_handler_done:

    mtcHttpUtil_log_event ( node_ptr->sysinvEvent );

    /* This is needed to get out of the loop */
    event_base_loopbreak((struct event_base *)arg);
}

/* The Inventory 'Add' request handler wrapper abstracted from nodeLinkClass */
void mtcInvApi_add_Handler ( struct evhttp_request *req, void *arg )
{
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    obj_ptr->mtcInvApi_add_handler ( req , arg );
}

/* The Inventory 'Qry' request handler wrapper abstracted from nodeLinkClass */
void mtcInvApi_qry_Handler ( struct evhttp_request *req, void *arg )
{
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    obj_ptr->mtcInvApi_qry_handler ( req , arg );
}

/* The Inventory 'Get' request handler wrapper abstracted from nodeLinkClass */
void mtcInvApi_get_Handler ( struct evhttp_request *req, void *arg )
{
    nodeLinkClass * obj_ptr = get_mtcInv_ptr () ;
    obj_ptr->mtcInvApi_get_handler ( req , arg );
}
