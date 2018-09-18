#ifndef __INCLUDE_MTCINVAPI_H__
#define __INCLUDE_MTCINVAPI_H__

/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform - Maintenance - Inventory Access API Header
  */

/**
  * @addtogroup mtcInvApi
  * @{
  *
  * This file implements the a set of mtcInvApi utilities that maintenance
  * calls upon to set/get host information to/from the sysinv database.
  *
  * The APIs exposed from this file are
  *
  *
  *   mtcInvApi_read_inventory   - Reads all the host inventory records from the
  *                                sysinv database in a specified batch number.
  *
  *   mtcInvApi_add_host         - Adds a host to the sysinv database.
  *
  *   mtcInvApi_load_host        - Loads the inventory content for a specified host.
  *
  *   See nodeClass.h for these prototypes
  *
  *   mtcInvApi_update_task      - Updates the task field of the specified host.
  *
  *   mtcInvApi_update_uptime    - Updates the uptime of the specified host.
  *
  *   mtcInvApi_update_value     - Updates any field of the specified host.
  *
  *   mtcInvApi_update_state     - Updates a maintenance state of specified host.
  *
  *   mtcInvApi_update_states    - Updates all maintenance states of specified host.
  *
  *
  *   Each utility is paired with a private handler.
  *
  *   mtcInvApi_get_handler      - handles response for mtcInvApi_read_inventory
  *
  *   mtcInvApi_add_Handler      - handles response for mtcInvApi_add_host
  *
  *   mtcInvApi_qry_handler      - handles response for mtcInvApi_load_host
  *
  *   mtcInvApi_upd_handler      - handles response for all update utilities
  *
  * Warning: These calls cannot be nested.
  *
  **/

#include "mtcHttpUtil.h"    /* for mtcHttpUtil_libEvent_init
                                   mtcHttpUtil_api_request
                                   mtcHttpUtil_log_event */

#define MTC_INV_LABEL           "/v1/ihosts/"    /**< inventory host url label          */
#define MTC_INV_IUSER_LABEL     "/v1/iuser/"     /**< inventory config url label        */
#define MTC_INV_BATCH           "?limit="        /**< batch read limit specified prefix */
#define MTC_INV_BATCH_MAX       5                /**< maximum allowed batched read      */

int mtcInvApi_handler ( libEvent & event );

/** Load all the host inventory from the sysinv database
  *
  * This API is only ever called once ; during initialization
  * to get an initial snapshot of the host information in
  * the database.
  *
  * @param batch - the number of inventory hosts' info to get in
  *                each request ; until all the elements are read.
  *
  * @return execution status
  *
  *- PASS - indicates successful send request
  *- FAIL_TIMEOUT - no response received in timeout period
  *- FAIL_JSON_PARSE - response json string did not parse properly
  *- HTTP status codes - any standard HTTP codes
  *
  *****************************************************************************/
int mtcInvApi_read_inventory ( int batch );

/** Add a host to the sysinv database
  *
  * This API is only ever called once ; during initialization
  * to add controller-0 to the database.
  *
  * @param info - reference to a structure containing the inventory elements
  *               to add.
  *
  * @return execution status
  *
  *- PASS  indicates successful send request
  *- FAIL  indicates a failed send request (may have refined failure codes)
  *
  *****************************************************************************/
int mtcInvApi_add_host ( node_inv_type & info );

/** Load all the elements of a host from the sysinv database.
  *
  * @param hostname - reference to a name string of the host to load.
  * @param info     - reference to a struct where the host info loaded from
  *                   the database will be put.
  *
  * @return execution status
  *
  *- PASS - indicates successful send request
  *- FAIL_TIMEOUT - no response received in timeout period
  *- FAIL_JSON_PARSE - response json string did not parse properly
  *- HTTP status codes - any standard HTTP codes
  *
  *****************************************************************************/
int mtcInvApi_load_host  ( string        & hostname ,
                           node_inv_type & inv_info );

/** @} mtcInvApi */

#endif /* __INCLUDE_MTCINVAPI_H__ */
