
/*
 * Copyright (c) 2016-2017, 2024 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River Titanium Cloud Hardware Monitor Threads Implementation"
 *
 */


#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/stat.h>

using namespace std;

#include "daemon_common.h"

#include "nodeBase.h"        /* for ... mtce node common definitions     */
#include "bmcUtil.h"         /* for ... mtce-common board management     */
#include "hostUtil.h"        /* for ... mtce host common definitions     */
#include "jsonUtil.h"        /* for ... common Json utilities            */
#include "bmcUtil.h"
#include "nodeMacro.h"
#include "threadUtil.h"
#include "hwmonThreads.h"    /* for ... BMC_THREAD_CMD__READ_SENSORS */
#include "hwmonBmc.h"        /* for ... MAX_IPMITOOL_PARSE_ERRORS         */
#include "hwmonClass.h"      /* for ... thread_extra_info_type            */
#include "nodeUtil.h"        /* for ... fork_execv                        */


/* One instance per thread. Uses the memory allocated for the stack.
 *
 * Although thread_local variables are not on the stack, they still
 * consume memory that’s tied to the thread’s overall resources,
 * and that memory often comes from the same per-thread allocation
 * that includes the stack ; refer to TLS (Thread-Local Storage).
 * The TLS area is often allocated adjacent to or within the thread's
 * stack mapping. A large thread_local variable increases the TLS
 * memory requirement, and if it exceeds the reserved space or
 * overlaps with the stack space, the OS may fail to allocate the
 * thread with a errno "Resource temporarily unavailable".
 * This allocation required the per thread stack to be increased. */
thread_local bmc_sample_type _sample_list[MAX_HOST_SENSORS];

// #define WANT_SAMPLE_LIST_DEBUG
#ifdef WANT_SAMPLE_LIST_DEBUG
void print_sample_list ( string & hostname )
{
    bool empty = false ;
    for ( int i = 0 ; i < MAX_HOST_SENSORS ; i++)
    {
        if ( strlen ( _sample_list[i].name ) != 0 )
        {
            if ( empty )
            {
                slog ("%s has sparse sensor list ; gap at %d", hostname.c_str(), i);
                empty = false ;
            }
            ilog ("%s Sample %d: %s - %s - %s - %s ... %s - %s - %s - %s - %s - %s",
                hostname.c_str(), i,
                _sample_list[i].name,
                _sample_list[i].value,
                _sample_list[i].unit,
                _sample_list[i].status,
                _sample_list[i].lnr,
                _sample_list[i].lcr,
                _sample_list[i].lnc,
                _sample_list[i].unc,
                _sample_list[i].ucr,
                _sample_list[i].unr);
        }
        else
        {
            empty = true ;
        }
    }
}
#endif // WANT_SAMPLE_LIST_DEBUG

/***************************************************************************
 *
 *               P R I V A T E       I N T E R F A C E S
 *
 **************************************************************************/

static void _command_not_supported ( thread_info_type * info_ptr )
{
    info_ptr->data = "{\"" ;
    info_ptr->data.append(BMC_JSON__SENSOR_DATA_MESSAGE_HEADER);
    info_ptr->data.append("\":{");
    info_ptr->data.append("\"status\":");
    info_ptr->data.append(itos(info_ptr->status));
    info_ptr->data.append(",");
    info_ptr->data.append("\"status_string\":\"command '");
    info_ptr->data.append(itos(info_ptr->command));
    info_ptr->data.append("' not supported\"}}");

    wlog_t ("%s %s\n", info_ptr->log_prefix, info_ptr->data.c_str());
}


static void _add_json_sensor_tuple ( bmc_sample_type * ptr, string & response )
{
    response.append ("{\"n\":\"");
    response.append (ptr->name);
    response.append ("\",\"v\":\"");
    response.append (ptr->value);
    response.append ("\",\"u\":\"");
    response.append (ptr->unit);
    response.append ("\",\"s\":\"");
    response.append (ptr->status);
    response.append ("\"");

    /* Include the threshold value of each below if not 'na' */
    if ( strcmp (ptr->lnr,"na" ))
    {
        response.append (",\"lnr\":\"");
        response.append (ptr->lnr);
        response.append ("\"");
    }
    if ( strcmp (ptr->lcr,"na" ))
    {
        response.append (",\"lcr\":\"");
        response.append (ptr->lcr);
        response.append ("\"");
    }
    if ( strcmp (ptr->lnc,"na" ))
    {
        response.append (",\"lnc\":\"");
        response.append (ptr->lnc);
        response.append ("\"");
    }
    if ( strcmp (ptr->unc,"na" ))
    {
        response.append (",\"unc\":\"");
        response.append (ptr->unc);
        response.append ("\"");
    }
    if ( strcmp (ptr->ucr,"na" ))
    {
        response.append (",\"ucr\":\"");
        response.append (ptr->ucr);
        response.append ("\"");
    }
    if ( strcmp (ptr->unr,"na" ))
    {
        response.append (",\"unr\":\"");
        response.append (ptr->unr);
        response.append ("\"");
    }
    response.append("}");
}

/*****************************************************************************
 *
 * Name        : _parse_sensor_data
 *
 * Description: Create a sensor data json string using pertinent data in the
 *              control structure data and of course the _sample_list.
 *
 *****************************************************************************/

static void _parse_sensor_data ( thread_info_type * info_ptr )
{
    if ( info_ptr && info_ptr->extra_info_ptr )
    {
        /*
         *   Get local copies rather than continuously use
         *   the pointer in the parse process ; just safer
         */
        thread_extra_info_type * extra_info_ptr = (thread_extra_info_type*)info_ptr->extra_info_ptr ;
        int samples = extra_info_ptr->samples ;

        info_ptr->data = "{\"" ;
        info_ptr->data.append (BMC_JSON__SENSOR_DATA_MESSAGE_HEADER);
        info_ptr->data.append ("\":{\"status\":");
        info_ptr->data.append(itos(info_ptr->status));
        info_ptr->data.append(",");
        info_ptr->data.append("\"status_string\":\"");

        if ( info_ptr->status == PASS )
        {
            info_ptr->data.append("pass\"");
        }
        else
        {
            info_ptr->data.append(info_ptr->status_string);
            info_ptr->data.append ("}}"); /* success path */
        }

        info_ptr->data.append (",\"");
        info_ptr->data.append (BMC_JSON__SENSORS_LABEL);
        info_ptr->data.append ("\":[");

#ifdef WANT_SAMPLE_LIST_DEBUG
        print_sample_list ( info_ptr->hostname );
#endif // WANT_SAMPLE_LIST_DEBUG

        for ( int i = 0 ; i < samples ; )
        {
            _add_json_sensor_tuple ( &_sample_list[i], info_ptr->data ) ;
            if ( ++i < samples )
                info_ptr->data.append (",");
        }
        info_ptr->data.append ("]");

        info_ptr->data.append ("}}"); /* success path */
        blog3_t ("%s %s\n", info_ptr->log_prefix, info_ptr->data.c_str());
    }
    else if ( info_ptr )
    {
        info_ptr->status_string = "null 'extra info' pointer" ;
        info_ptr->status = FAIL_NULL_POINTER ;
    }
}

/*****************************************************************************
 *
 * Name       : _get_field
 *
 * Description: Assumes a specific string format where fields are delimted
 *              with the '|' character.
 *
 * Warnings   : The src and dst variable track the src_ptr and dst_ptr to
 *              ensure we never run longer than that string length
 *
 *              IPMITOOL_MAX_FIELD_LEN for dst_ptr and
 *              IPMITOOL_MAX_LINE_LEN for src_ptr
 *
 *              A parse error causes dst_ptr to be updated with
 *              PARSE_ERROR_STR and return.
 *
 * Assumptions: Extra white spaces at the beginning and end of a field
 *              are removed. There may or may not be such white spaces.
 *
 * Field 0        1          2        3     4   5    6       7        8     9
 * ----------+--------+-------------+----+----+----+----+--------+--------+----+
 * Temp_CPU1 | 42.000 | % degrees C | ok | na | na | na | 86.000 | 87.000 | na
 *
 *****************************************************************************/

#define PARSE_ERROR_STR ((const char *)("parse error"))

void _ipmitool_get_field ( char * src_ptr , int field, char * dst_ptr )
{
    int src = 0 ;
    char * saved_dst_ptr = dst_ptr ;

    /* advance to requested field */
    for ( int y = 0 ; y < field ; src_ptr++, src++ )
    {
        /* error detection */
        if (( *src_ptr == '\0' ) || ( src >= IPMITOOL_MAX_LINE_LEN ))
        {
            goto _get_field_parse_error1 ;
        }
        if ( *src_ptr == '|' )
        {
            y++ ;
        }
    }

    /* eat first white-space(s) */
    for ( ; *src_ptr == ' ' ; src_ptr++ , src++)
    {
        /* error detection */
        if ( src >= IPMITOOL_MAX_LINE_LEN )
        {
            goto _get_field_parse_error2 ;
        }
    }

    /* copy the source to destination ; until we see a '|' */
    for ( int dst = 0 ;  ; src_ptr++ , dst_ptr++ , src++ , dst++ )
    {
        unsigned char ch = 0 ;

        /* error detection */
        if ( src >= IPMITOOL_MAX_LINE_LEN )
        {
            goto _get_field_parse_error3 ;
        }
        if ( dst >= IPMITOOL_MAX_FIELD_LEN )
        {
            goto _get_field_parse_error4 ;
        }
        ch = *src_ptr ;
        if (( ch != '|' ) && ( ch != '\0' ) && ( ch != 10 ) && ( ch != 13 ))
        {
            *dst_ptr = ch ;
        }
        else
            break ;
    }

    /* remove last space(s) if they exists */
    for ( dst_ptr-- ; *dst_ptr == ' ' ; dst_ptr-- ) { *dst_ptr = '\0' ; }

    /* terminate the line after the last real non-space char */
    *(++dst_ptr) = '\0' ;

    return ;

_get_field_parse_error1:
    wlog_t ("%s 1\n", PARSE_ERROR_STR );
    snprintf ( saved_dst_ptr , strlen(PARSE_ERROR_STR)+1, "%s", PARSE_ERROR_STR );
    return ;

_get_field_parse_error2:
    wlog_t ("%s 2\n", PARSE_ERROR_STR );
    snprintf ( saved_dst_ptr , strlen(PARSE_ERROR_STR)+1, "%s", PARSE_ERROR_STR );
    return ;

_get_field_parse_error3:
    wlog_t ("%s 3\n", PARSE_ERROR_STR );
    snprintf ( saved_dst_ptr , strlen(PARSE_ERROR_STR)+1, "%s", PARSE_ERROR_STR );
    return ;

_get_field_parse_error4:
    wlog_t ("%s 4\n", PARSE_ERROR_STR);
    snprintf ( saved_dst_ptr , strlen(PARSE_ERROR_STR)+1, "%s", PARSE_ERROR_STR );
    return ;

}

/* Temp_CPU1        | 42.000     | % degrees C | ok    | na        | na        | na        | 86.000    | 87.000    | na */
#define IPMITOOL_FULL_OUTPUT_COLUMNS  (10)

void * hwmonThread_ipmitool ( void * arg )
{
    int samples ;

    thread_info_type       * info_ptr  ;
    thread_extra_info_type * extra_ptr ;
    int parse_errors = 0 ;

    /* Pointer Error Detection and Handling */
    if ( !arg )
    {
        slog ("*** ipmitool thread called with null arg pointer *** corruption\n");
        return NULL ;
    }

    /* cast pointers from arg */
    info_ptr  = (thread_info_type*)arg   ;
    extra_ptr = (thread_extra_info_type*)info_ptr->extra_info_ptr ;

    info_ptr->pw_file_fd = 0 ;

    /* allow the parent to confirm thread id */
    info_ptr->id = pthread_self() ;
    if ( extra_ptr == NULL )
    {
        info_ptr->status_string = "null 'extra info' pointer" ;
        info_ptr->status = FAIL_NULL_POINTER ;
        goto ipmitool_thread_done ;
    }

    /* Set cancellation option so that a delete operation
     * can kill this thread immediately */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

    /* the number of sensors are learned */
    extra_ptr->samples = samples = 0 ;
    MEMSET_ZERO (_sample_list);
    switch ( info_ptr->command )
    {
        case BMC_THREAD_CMD__POWER_STATUS:
        {
            int rc = PASS ;

            info_ptr->status_string = "" ;
            info_ptr->status = PASS ;
            string command = IPMITOOL_POWER_STATUS_CMD ;

            blog2_t ("%s query power status\n", info_ptr->log_prefix);

            if ( info_ptr->extra_info_ptr == NULL )
            {
                info_ptr->status = FAIL_NULL_POINTER ;
                info_ptr->status_string = "null extra info pointer" ;
                goto ipmitool_thread_done ;
            }

            /**************** Create the password file *****************/
            bmcUtil_create_pw_file ( info_ptr, extra_ptr->bm_pw, BMC_PROTOCOL__IPMITOOL) ;
            if ( info_ptr->password_file.empty() )
            {
                info_ptr->status_string = "failed to get a temporary password filename" ;
                info_ptr->status = FAIL_FILE_CREATE ;
                goto ipmitool_thread_done ;
            }
            dlog_t ("%s password filename     : %s\n",
                        info_ptr->log_prefix,
                        info_ptr->password_file.c_str());

            /*************** Create the output filename ***************/
            string datafile =
            bmcUtil_create_data_fn (info_ptr->hostname,
                                    BMC_POWER_STATUS_FILE_SUFFIX,
                                    BMC_PROTOCOL__IPMITOOL ) ;

            dlog_t ("%s power query filename  : %s\n",
                        info_ptr->log_prefix,
                        datafile.c_str());

            /************** Create the ipmitool request **************/
            string request =
            ipmiUtil_create_request ( command,
                                      extra_ptr->bm_ip,
                                      extra_ptr->bm_un,
                                      info_ptr->password_file);
            dlog_t ("%s power status query cmd: %s\n",
                        info_ptr->log_prefix,
                        request.c_str());

            if ( daemon_is_file_present ( MTC_CMD_FIT__POWER_STATUS ))
            {
                slog ("%s FIT IPMITOOL_POWER_STATUS_CMD\n", info_ptr->hostname.c_str());
                rc = PASS ;
            }
            else
            {
                /* Make the request */
                rc = fork_execv ( info_ptr->hostname, request, datafile ) ;
            }

            unlink(info_ptr->password_file.data());
            daemon_remove_file (info_ptr->password_file.data());

            /* check for system call error case */
            if ( rc != PASS )
            {
                info_ptr->status_string = "failed power status query ; " ;
                info_ptr->status_string.append(request);
                info_ptr->status = FAIL_SYSTEM_CALL ;
            }
            else
            {
                bool datafile_present = false ;

                /* look for the output data file */
                for ( int i = 0 ; i < 10 ; i++ )
                {
                    pthread_signal_handler ( info_ptr );
                    if ( daemon_is_file_present ( datafile.data() ))
                    {
                        datafile_present = true ;
                        break ;
                    }
                    info_ptr->progress++ ;
                    sleep (1);
                }

                if ( datafile_present )
                {
                    info_ptr->data = daemon_read_file (datafile.data()) ;
                    dlog_t ("%s data:%s\n",
                                info_ptr->hostname.c_str(),
                                info_ptr->data.data());

                    info_ptr->status_string = "pass" ;
                    info_ptr->status = PASS ;
                }
                else
                {
                    info_ptr->status_string = "command did not produce output file ; timeout" ;
                    info_ptr->status = FAIL_FILE_ACCESS ;
                }
            }
            break ;
        }
        case BMC_THREAD_CMD__READ_SENSORS:
        {
            int rc = PASS ;

            info_ptr->status_string = "" ;
            info_ptr->status = PASS ;

            blog3_t ("%s read sensors request\n", info_ptr->log_prefix);

            if ( info_ptr->extra_info_ptr == NULL )
            {
                info_ptr->status = FAIL_NULL_POINTER ;
                info_ptr->status_string = "null extra info pointer" ;
                goto ipmitool_thread_done ;
            }

            bmcUtil_create_pw_file ( info_ptr,
                                     extra_ptr->bm_pw,
                                     BMC_PROTOCOL__IPMITOOL);

            if ( info_ptr->password_file.empty() )
            {
                info_ptr->status_string = "failed to get a temporary password filename" ;
                info_ptr->status = FAIL_FILE_CREATE ;
                goto ipmitool_thread_done ;
            }

            dlog_t ("%s password filename     : %s\n",
                        info_ptr->log_prefix,
                        info_ptr->password_file.c_str());

            /*************** Create the output filename ***************/
            string sensor_datafile =
            bmcUtil_create_data_fn (info_ptr->hostname,
                                    BMC_SENSOR_OUTPUT_FILE_SUFFIX,
                                    BMC_PROTOCOL__IPMITOOL ) ;

            dlog_t ("%s sensor output file%s\n",
                      info_ptr->log_prefix,
                      sensor_datafile.c_str());

            /************** Create the ipmitool request **************/
            string sensor_query_request =
            ipmiUtil_create_request ( IPMITOOL_SENSOR_QUERY_CMD,
                                      extra_ptr->bm_ip,
                                      extra_ptr->bm_un,
                                      info_ptr->password_file);

            dlog_t ("%s sensor query cmd:%s\n",
                  info_ptr->log_prefix,
                  sensor_query_request.c_str());


            /****************************************************************
             *
             * This fault insertion case is added for PV.
             * If MTC_CMD_FIT__SENSOR_DATA file is present and
             * there is an existing datafile then no ipmitool
             * sensor read is performed. Instead, a raw output file can be
             * manually updated and used to perform sensor fault insertion.
             *
             *****************************************************************/
            if (( daemon_is_file_present ( MTC_CMD_FIT__SENSOR_DATA )) &&
                ( daemon_is_file_present ( sensor_datafile.data())))
            {
                ilog_t ("%s bypass sensor data read ; %s FIT file is present",
                            info_ptr->hostname.c_str(),
                            MTC_CMD_FIT__SENSOR_DATA);
                rc = PASS ;
            }
#ifdef WANT_FIT_TESTING
            else if ( daemon_want_fit ( FIT_CODE__HWMON__AVOID_SENSOR_QUERY, info_ptr->hostname ))
            {
                rc = PASS ; // ilog ("%s FIT Avoiding Sensor Query\n", info_ptr->hostname.c_str());
            }
            else if ( daemon_want_fit ( FIT_CODE__AVOID_N_FAIL_BMC_REQUEST, info_ptr->hostname ))
            {
                rc = FAIL ; // ilog ("%s FIT Avoiding Sensor Query\n", info_ptr->hostname.c_str());
            }
#endif
            else
            {
                /* remove the last query */
                // daemon_remove_file ( sensor_datafile.data() ) ;
                rc = fork_execv ( info_ptr->hostname, sensor_query_request, sensor_datafile ) ;
            }

#ifdef WANT_FIT_TESTING
            if ( daemon_want_fit ( FIT_CODE__THREAD_TIMEOUT, info_ptr->hostname ) )
            {
                for ( ; ; )
                {
                    pthread_signal_handler ( info_ptr );
                    sleep (1);
                }
            }
            if ( daemon_want_fit ( FIT_CODE__THREAD_SEGFAULT, info_ptr->hostname ) )
            {
                daemon_do_segfault();
            }
#endif

            unlink(info_ptr->password_file.data());
            daemon_remove_file (info_ptr->password_file.data());

            /* Debug Option - enable lane debug_bmgt3 = 8 and touch
            * /var/run/bmc/ipmitool/want_dated_sensor_data_files for ipmi
            * or
            * /var/run/bmc/redfishtool/want_dated_sensor_data_files for redfish
            *
            * ... to save ther current sensor read file with a dated extension
            *     so that a read history is maintained for debug purposes. */
            if(daemon_get_cfg_ptr()->debug_bmgmt&8)
                if ( daemon_is_file_present (WANT_DATED_IPMI_SENSOR_DATA_FILES))
                    daemon_copy_file(info_ptr->hostname, sensor_datafile.data());

            /* check for system call error case */
            if ( rc != PASS )
            {
                info_ptr->status_string = "failed query ; " ;
                info_ptr->status_string.append(sensor_query_request);
                info_ptr->status = FAIL_SYSTEM_CALL ;
            }
            else
            {
                FILE * _fp = fopen ( sensor_datafile.data(), "r" );
                if ( _fp )
                {
                    char buffer [IPMITOOL_MAX_LINE_LEN];
                    int  line  = 0    ;
                    while ( fgets (buffer, IPMITOOL_MAX_LINE_LEN, _fp) != NULL )
                    {
                        if ( strnlen ( buffer, IPMITOOL_MAX_LINE_LEN ) )
                        {
                            int bars              = 0     ; /* tracks the number of '|'s found in a line   */
                            bool long_field_error = false ; /* set to true if we get a field in line error */
                            int  char_field_count = 0     ; /* counts the number of characters in a field  */
                            // ilog ("\n");
                            // ilog ("ipmitool:%d:%s\n", line, buffer );

                            /****************************************
                             * sanity check the ipmitool output
                             *
                             * ipmitool line output looks like
                             *
                             * Temp_CPU1 | 42.000 | % degrees C | ok | na | na | na | 86.000 | 87.000 | na
                             *
                             *****************************************
                             * start at 1 to handle the 'i-1' case   */

                            int i = 1 ; /* aka character in line count or index */
                            while (( buffer[i] != '\0' ) && ( i < IPMITOOL_MAX_LINE_LEN ))
                            {
                                if ( buffer[i] == '|' )
                                {
                                    if ( char_field_count > IPMITOOL_MAX_FIELD_LEN )
                                    {
                                        long_field_error = true ;
                                    }
                                    char_field_count = 0 ;
                                    ++bars ;
                                }
                                ++char_field_count ;
                                i++ ; /* advance through the line, character by character */
                            }

                            /* scan the sample as long as no field exceeds the max string length */
                            if ( long_field_error == false )
                            {
                                /* Only process properly formatted lines that
                                 * don't have field lengths longer than IPMITOOL_MAX_FIELD_LEN*/
                                if ( bars == (IPMITOOL_FULL_OUTPUT_COLUMNS-1) )
                                {
                                    char type[IPMITOOL_MAX_FIELD_LEN] ;
                                    int i = 0 ;
                                    int x = 0 ;
                                    int y = 0 ;
                                    /* get type */
                                    /* advance to type field */
                                    for ( i = 0 , y = 0 ; y < 2 ; i++ )
                                    {
                                        /* handle case where we cant find the '|'s and y never reaches 2 */
                                        if ( i < IPMITOOL_MAX_LINE_LEN )
                                        {
                                            if ( buffer[i] == '|' )
                                            {
                                                y++ ;
                                            }
                                        }
                                        else
                                        {
                                            if ( ++parse_errors == MAX_IPMITOOL_PARSE_ERRORS )
                                            {
                                                info_ptr->status = FAIL_JSON_TOO_LONG ;
                                                info_ptr->status_string = "sensor format error ; line format error";
                                                goto ipmitool_thread_done ;
                                            }
                                            break ;
                                        }
                                    }

                                    /* ignore this line */
                                    if ( i >= IPMITOOL_MAX_LINE_LEN )
                                    {
                                        continue ;
                                    }

                                    /* eat first white-space(s) */
                                    for ( ; buffer[i] == ' ' ; i++ ) ;

                                    /* copy the senor unit type to type */
                                    for ( x = 0 ; buffer[i] != '|' ; i++, x++ ) { type[x] = buffer[i] ; }

                                    /* remove last space(s) if they exists */
                                    for ( x-- ; type[x] == ' ' ; x-- ) { type[x] = '\0' ; }

                                    /* terminate the line after the last real non-space char */
                                    type[x+1] = '\0' ;

                                    if (!strlen(type))
                                    {
                                        blog3_t ("%s skipping sensor with empty unit type\n", info_ptr->log_prefix);
                                        blog3_t ("%s ... line:%d - %s", info_ptr->log_prefix, line, buffer );
                                        continue ;
                                    }
                                    else
                                    {
                                        blog3_t ("%s Line:%d is a '%s' sensor\n", info_ptr->log_prefix, line, type );
                                    }

                                    _ipmitool_get_field ( buffer, 0, _sample_list[samples].name   );
                                    _ipmitool_get_field ( buffer, 1, _sample_list[samples].value  );

                                    /* copy already learned type to unit field 2 */
                                    snprintf ( _sample_list[samples].unit, strlen(type)+1, "%s", type );

                                    _ipmitool_get_field ( buffer, 3, _sample_list[samples].status );
                                    _ipmitool_get_field ( buffer, 4, _sample_list[samples].lnr );
                                    _ipmitool_get_field ( buffer, 5, _sample_list[samples].lcr );
                                    _ipmitool_get_field ( buffer, 6, _sample_list[samples].lnc );
                                    _ipmitool_get_field ( buffer, 7, _sample_list[samples].unc );
                                    _ipmitool_get_field ( buffer, 8, _sample_list[samples].ucr );
                                    _ipmitool_get_field ( buffer, 9, _sample_list[samples].unr );
                                    blog2_t ("%s | %20s | %8s | %12s | %3s | %8s | %8s | %8s | %8s | %8s | %8s |\n",
                                           info_ptr->log_prefix,
                                           _sample_list[samples].name,
                                           _sample_list[samples].value,
                                           _sample_list[samples].unit,
                                           _sample_list[samples].status,
                                           _sample_list[samples].lnr,
                                           _sample_list[samples].lcr,
                                           _sample_list[samples].lnc,
                                           _sample_list[samples].unc,
                                           _sample_list[samples].ucr,
                                           _sample_list[samples].unr);
                                    samples++ ;
                                    if ( samples >= MAX_HOST_SENSORS )
                                    {
                                        samples-- ;
                                        rc = info_ptr->status = FAIL_OUT_OF_RANGE ;
                                        info_ptr->status_string = "max number of sensors reached";
                                        break ;
                                    }
                                    rc = PASS ;
                                }
                                else
                                {
                                    /* ignore commented lines */
                                    if (( buffer[0] != '#' ) && ( buffer[0] != ';' ))
                                    {
                                        if ( ++parse_errors == MAX_IPMITOOL_PARSE_ERRORS )
                                        {
                                            info_ptr->status = FAIL_BAD_PARM ;
                                            info_ptr->status_string = "sensor format error ; line format error 1";
                                        }
                                        blog3_t ("%s %s (e:%d d:%d)", info_ptr->log_prefix,
                                                                      info_ptr->status_string.c_str(),
                                                                      (IPMITOOL_FULL_OUTPUT_COLUMNS), bars+1 );

                                        blog3_t ("%s ... line:%d - %s", info_ptr->log_prefix, line, buffer );
                                    }
                                    else
                                    {
                                        blog3_t ("%s COMMENT %s", info_ptr->log_prefix, &buffer[0]);
                                    }
                                } /* end else */
                            }
                            else
                            {
                                if ( ++parse_errors == MAX_IPMITOOL_PARSE_ERRORS )
                                {
                                    info_ptr->status = FAIL_JSON_TOO_LONG ;
                                    info_ptr->status_string = "sensor format error ; line format error 2" ;
                                }
                                blog3_t ("%s ... line:%d - %s", info_ptr->log_prefix, line, buffer );
                            }
                        }
                        MEMSET_ZERO(buffer) ;
                        line++ ;
                        pthread_signal_handler ( info_ptr );
                    } /* end while loop */

                    extra_ptr->samples = samples ;

                    if ( samples == 0 )
                    {
                        info_ptr->status = FAIL_NO_DATA ;
                        info_ptr->status_string = "no sensor data found";
                    }
                    fclose(_fp);
                } /* fopen */
                else
                {
                    info_ptr->status = FAIL_FILE_ACCESS ;
                    info_ptr->status_string = "failed to open sensor data file: ";
                    info_ptr->status_string.append(sensor_datafile);
                 }
            } /* end else handling of successful system command */
            break ;
        }
        default:
        {
            info_ptr->status = FAIL_BAD_CASE ;
            _command_not_supported ( info_ptr );
            break ;
        }
    }

ipmitool_thread_done:

    if ( info_ptr->pw_file_fd > 0 )
        close(info_ptr->pw_file_fd);
    info_ptr->pw_file_fd = 0 ;

    if ( ! info_ptr->password_file.empty() )
    {
        unlink(info_ptr->password_file.data());
        daemon_remove_file ( info_ptr->password_file.data() ) ;
        info_ptr->password_file.clear();
    }

    pthread_signal_handler ( info_ptr );

    /* Sensor reading specific exit */
    if ( info_ptr->command == BMC_THREAD_CMD__READ_SENSORS )
    {
        if ( parse_errors )
        {
            wlog_t ("%s exiting with %d parse errors (rc:%d)\n",
                        info_ptr->log_prefix, parse_errors, info_ptr->status);
        }
        else
        {
            dlog_t ("%s exit", info_ptr->log_prefix );
        }
        _parse_sensor_data ( info_ptr );
    }

    info_ptr->progress++ ;
    info_ptr->runcount++ ;
    info_ptr->id = 0     ;
    pthread_exit (&info_ptr->status );
    return NULL ;
}

/*****************************************************************************
 *
 * Name        : _set_default_unit_type_for_sensor
 * Description : Set default unit type for sensor
 * Parameters  : label   - sensor label
                 samples - sensor index in global_sample_list array
 *
 *****************************************************************************/

static void _set_default_unit_type_for_sensor( thread_info_type * info_ptr,
                                               string label, int samples)
{
    if ( label == REDFISH_SENSOR_LABEL_VOLT )
    {
        strcpy( _sample_list[samples].unit, BMC_SENSOR_DEFAULT_UNIT_TYPE_VOLT );
    }
    else if ( label == REDFISH_SENSOR_LABEL_TEMP )
    {
        strcpy( _sample_list[samples].unit, BMC_SENSOR_DEFAULT_UNIT_TYPE_TEMP);
    }
    else if (( label == REDFISH_SENSOR_LABEL_POWER_CTRL ) ||
             ( label == REDFISH_SENSOR_LABEL_POWER_REDUNDANCY ) ||
             ( label == REDFISH_SENSOR_LABEL_POWER_SUPPLY ))
    {
        strcpy( _sample_list[samples].unit, BMC_SENSOR_DEFAULT_UNIT_TYPE_POWER);
    }
    else if ( label == REDFISH_SENSOR_LABEL_FANS )
    {
        strcpy( _sample_list[samples].unit, BMC_SENSOR_DEFAULT_UNIT_TYPE_FANS);
    }
    else
    {
        dlog_t ("%s unrecognized label\n", info_ptr->log_prefix);
    }
}

/*****************************************************************************
 *
 * Name        : _parse_redfish_sensor_data
 * Purpose     : Parse redfish command response
 * Description : Parse json string and store sensor data to  _sample_list.
 * Parameters  : json_str_ptr  - the json string read from the file of command response.
                 info_ptr      - thread info
                 label         - json key, like "Voltages", "PowerControl"
                 reading_label - json key, like "ReadingVolts", "PowerConsumedWatts"
                 samples       - sensor data index for _sample_list array.
 * Returns     : PASS if parse data successfully.
 *
 *****************************************************************************/

/* Get value from json string according to key, if value is none, return na
   Put the value to _sample_list */
#define GET_SENSOR_DATA_VALUE( temp_str, json_obj, key, para )     \
    temp_str = jsonUtil_get_key_value_string ( json_obj, key );    \
    if ( !strcmp (temp_str.data(),"none" ))   temp_str = "na" ;    \
    strcpy( _sample_list[samples].para , temp_str.c_str() );

static int _parse_redfish_sensor_data( char * json_str_ptr, thread_info_type * info_ptr,
                                       string label, const char * reading_label, int & samples )
{
    int rc = PASS ;

    /*************************************************************************
     *
     * Gracefully handle a missing sensor group label.
     * Return failure, that is ignored, if its not there.
     *
     * Calling jsonUtil_get_list directly results in noisy json error logs.
     * If a server is not providing a canned group then so be it.
     *
     *************************************************************************
     *
     * Start by objectifying the output data followed by getting that
     * sensor group key value */
    struct json_object *json_obj = json_tokener_parse(json_str_ptr);
    if ( !json_obj )
        return (FAIL_JSON_PARSE);
    string value = jsonUtil_get_key_value_string ( json_obj, label.data());
    json_object_put(json_obj);
    if (( value.empty() || value == NONE ))
        return (FAIL_NO_DATA);

    std::list<string> sensor_list ;
    sensor_list.clear();
    rc = jsonUtil_get_list(json_str_ptr, label, sensor_list);
    if ( rc == PASS )
    {
        string status_str;
        string temp_str;
        std::list<string>::iterator iter_curr_ptr ;

        // Required for special case handling of the Power Supply Redundancy Sensor
        bool is_power_supply_redundancy_sensor = false ;
        int  redundancy_count = 0 ;

        for ( iter_curr_ptr  = sensor_list.begin();
              iter_curr_ptr != sensor_list.end() ;
            ++iter_curr_ptr )
        {
            json_obj = json_tokener_parse((char*)iter_curr_ptr->data());
            if ( !json_obj )
            {
                elog_t ("%s no or invalid sensor record\n", info_ptr->hostname.c_str());
                return (FAIL_JSON_PARSE);
            }

            /* Name   : GET_SENSOR_DATA_VALUE
             *
             * Purpose: Parse value from json string according to key.
             *
             * If value is none, return na.
             * Put the value to _sample_list.
             *
             * Start with just 'Name' and 'Reading'
             */
            GET_SENSOR_DATA_VALUE( temp_str, json_obj, "Name", name )
            GET_SENSOR_DATA_VALUE( temp_str, json_obj, reading_label, value )
            GET_SENSOR_DATA_VALUE( temp_str, json_obj, "ReadingUnits", unit )

            /* Abort on this sensor if the sensor name is missing.
             * A missing name is parsed as 'na' by GET_SENSOR_DATA_VALUE macro.
             *
             * This check was added after seeing a missing 'Name' and 'Status'
             * on one of the integration servers this feature was tested against.
             * Without this check the code will create a sensor with name = na */
            if ( !strcmp(_sample_list[samples].name, "na") )
            {
                /* Another special case handling
                 *
                 * Its a Fan sensor if ReadingUnits is RPM */
                 if ( strcmp(_sample_list[samples].unit, "RPM"))
                    return (FAIL_NOT_FOUND);

                 /* So it might be a Fan sensor. Still need a sensor name.
                  * Some Dell servers that publish the fan sensor name key
                  * as 'FanName' rather than 'Name' like all other sensors. */
                 GET_SENSOR_DATA_VALUE( temp_str, json_obj, "FanName", name )
                 if ( !strcmp(_sample_list[samples].name,"na") )
                    return (FAIL_NOT_FOUND);
                 strcpy( _sample_list[samples].unit, BMC_SENSOR_DEFAULT_UNIT_TYPE_FANS);
            }

            /* The Redundancy sensor does not have Upper/Lower threshold labels */
            if ( label.compare(REDFISH_SENSOR_LABEL_POWER_REDUNDANCY ) )
            {
                GET_SENSOR_DATA_VALUE( temp_str, json_obj, "LowerThresholdNonRecoverable", lnr )
                GET_SENSOR_DATA_VALUE( temp_str, json_obj, "LowerThresholdCritical", lcr )
                GET_SENSOR_DATA_VALUE( temp_str, json_obj, "LowerThresholdNonCritical", lnc )
                GET_SENSOR_DATA_VALUE( temp_str, json_obj, "UpperThresholdNonCritical", unc )
                GET_SENSOR_DATA_VALUE( temp_str, json_obj, "UpperThresholdCritical", ucr )
                GET_SENSOR_DATA_VALUE( temp_str, json_obj, "UpperThresholdNonRecoverable", unr )
            }
            else
            {
                is_power_supply_redundancy_sensor = true ;
                redundancy_count = atoi(_sample_list[samples].value);
                blog2_t ("%s redundancy count is %d", info_ptr->hostname.c_str(), redundancy_count );
            }
            /* Set default unit type if can not get unit type from json string */
            if ( !strcmp(_sample_list[samples].unit, "na") )
            {
                _set_default_unit_type_for_sensor( info_ptr, label, samples );
            }

            /* Parse and store status to _sample_list[samples].status */
            status_str = jsonUtil_get_key_value_string ( json_obj, "Status" );
            if ( strcmp (status_str.data(),"none" ))
            {
                struct json_object *json_status_obj = json_tokener_parse((char*)status_str.data());
                if ( json_status_obj )
                {
                    string state = jsonUtil_get_key_value_string ( json_status_obj, "State" );
                    string health = jsonUtil_get_key_value_string ( json_status_obj, "Health" );
                    if ( !strcmp (state.data(),"Enabled" ))
                    {
                        // This condition is to override the reported health status
                        // of the Power Supply Redundancy sensor from Critical,
                        // or not OK, to Major.
                        //
                        // Some servers report a Critical status when there is only
                        // a single power supply installed. We don't want to do that
                        // here because some systems may be intentionally provisioned
                        // with a single power supply to save cost. We don't want to
                        // alarm in that case.
                        //
                        // Furthermore, when there are 2 installed power supplies
                        // and one is failing or not plugged in some servers report
                        // that as Critical. We don't want to raise a Critical alarm
                        // simply due to a lack of redundancy. Critical alarms are
                        // reserved for service affecting error conditions.
                        //
                        // System administrators that wish to have an alarm for this
                        // case can choose to modify the hardware monitor server power
                        // group major action handling from the default 'log' to 'alarm'.
                        //
                        // In Summary, only assert the redundancy failure status if
                        //  - redundancy count of 2 and
                        //  - health reading is not OK
                        if (( is_power_supply_redundancy_sensor == true ) &&
                            ( strcmp (health.data(), REDFISH_SEVERITY__GOOD)))
                        {
                            if ( redundancy_count > 1 )
                                health = REDFISH_SEVERITY__MAJOR ;
                            else if ( redundancy_count == 0 )
                                health = "" ;
                            else
                                health = REDFISH_SEVERITY__GOOD ;
                        }

                        if ( !strcmp (health.data(), REDFISH_SEVERITY__GOOD ))
                        {
                            strcpy(_sample_list[samples].status, "ok");
                        }
                        else if  (!strcmp (health.data(), REDFISH_SEVERITY__MAJOR ))
                        {
                            strcpy(_sample_list[samples].status, "nc");
                        }
                        else if  (!strcmp (health.data(), REDFISH_SEVERITY__CRITICAL ))
                        {
                            strcpy(_sample_list[samples].status, "cr");
                        }
                        else if  (!strcmp (health.data(), REDFISH_SEVERITY__NONRECOVERABLE ))
                        {
                            strcpy(_sample_list[samples].status, "nr");
                        }
                        else
                        {
                            strcpy(_sample_list[samples].status, "na");
                        }
                    }
                    else
                    {
                        strcpy(_sample_list[samples].status, "na");
                    }
                    json_object_put(json_status_obj);
                }
            }
            else
            {
                strcpy(_sample_list[samples].status, "na");
            }

            if (json_obj) json_object_put(json_obj);

            samples++ ;
            if ( samples >= MAX_HOST_SENSORS )
            {
                samples-- ;
                rc = info_ptr->status = FAIL_OUT_OF_RANGE ;
                info_ptr->status_string = "max number of sensors reached";
                break ;
            }
        }
    }

    return (rc);
}

/*****************************************************************************
 *
 * Name        : _redfishUtil_send_request
 * Description : Construct redfishtool request and send it out
 * Parameters  : info_ptr              - thread info
                 datafile              - date file used for storing redfishtool comand response
                 file_suffix           - file suffix for datafile name
                 redfish_cmd_str       - redfish command string
 * Returns     : PASS if command sent out successfully.
 *
 *****************************************************************************/

static int _redfishUtil_send_request( thread_info_type * info_ptr, string & datafile,
                                      const char * file_suffix,    const char * redfish_cmd_str )
{
    string request = "" ;
    string config_file_content = "" ;
    int rc = PASS ;
    thread_extra_info_type * extra_ptr = (thread_extra_info_type*)info_ptr->extra_info_ptr ;

    info_ptr->status_string = "" ;
    info_ptr->status = PASS ;

    if ( extra_ptr == NULL )
    {
        info_ptr->status = FAIL_NULL_POINTER ;
        info_ptr->status_string = "null extra info pointer" ;
        return FAIL ;
    }

    /**************** Create the password file *****************/
    config_file_content = "{\"username\":\"" ;
    config_file_content.append(extra_ptr->bm_un) ;
    config_file_content.append("\",\"user\":\"") ;
    config_file_content.append(extra_ptr->bm_un) ;
    config_file_content.append("\",\"password\":\"") ;
    config_file_content.append(extra_ptr->bm_pw) ;
    config_file_content.append("\"}") ;

    bmcUtil_create_pw_file ( info_ptr, config_file_content, BMC_PROTOCOL__REDFISHTOOL ) ;
    if ( info_ptr->password_file.empty() )
    {
        info_ptr->status_string = "failed to get a temporary password filename" ;
        info_ptr->status = FAIL_FILE_CREATE ;
        return FAIL ;
    }

    dlog_t ("%s password filename     : %s\n",
                info_ptr->log_prefix,
                info_ptr->password_file.c_str());

    /*************** Create the output filename ***************/
    datafile = bmcUtil_create_data_fn (info_ptr->hostname, file_suffix, BMC_PROTOCOL__REDFISHTOOL ) ;
    dlog_t ("%s  create data filename  : %s\n", info_ptr->log_prefix, datafile.c_str());

    /************** Create the redfishtool request **************/
    request = redfishUtil_create_request ( redfish_cmd_str,
                                           extra_ptr->bm_ip,
                                           info_ptr->password_file);

    dlog_t ("%s query cmd: %s\n",
                info_ptr->log_prefix,
                request.c_str());

    if (( info_ptr->command == BMC_THREAD_CMD__READ_SENSORS ) &&
        ( daemon_is_file_present ( MTC_CMD_FIT__SENSOR_DATA )) &&
        ( daemon_is_file_present ( datafile.data())))
    {
        ilog_t ("%s bypass sensor data read ; %s FIT file is present",
                    info_ptr->hostname.c_str(),
                    MTC_CMD_FIT__SENSOR_DATA);
        rc = PASS ;
    }
    else
    {
        daemon_remove_file ( datafile.data() ) ;
        rc = threadUtil_bmcSystemCall (info_ptr->hostname,
                                       request,
                                       datafile,
                                       DEFAULT_SYSTEM_REQUEST_LATENCY_SECS) ;
        if ( rc != PASS )
        {
            /* crop garbage from the command to reduce log length */
            string cmd_only = bmcUtil_chop_system_req ( request ) ;
            elog_t ("%s system call failed [%s] (%d:%d:%m)\n",
                        info_ptr->hostname.c_str(),
                        cmd_only.c_str(),
                        rc, errno );

            info_ptr->status = FAIL_SYSTEM_CALL ;
            if ( daemon_is_file_present ( datafile.data() ))
            {
                /* load in the error. stdio is redirected to the datafile */
                info_ptr->status_string = daemon_read_file(datafile.data());
            }
            else
            {
                info_ptr->status_string = "system call failed for info query ; " ;
                info_ptr->status_string.append(request);
            }
        }
    }

    unlink(info_ptr->password_file.data());
    daemon_remove_file (info_ptr->password_file.data());
    return (rc) ;
}

/*****************************************************************************
 *
 * Name        : wait_for_command_output
 * Description : Wait for some time to check if redfishtool command output is available.
 * Parameters  : info_ptr     - thread info
                 datafile     - date file used for storing redfishtool comand response
 * Returns     : True if command response file is availalbe
                 False if command response file is unavailable after timeout.
 *
 *****************************************************************************/

static bool _wait_for_command_output( thread_info_type * info_ptr, string & datafile )
{
    /* look for the output data file */
    for ( int i = 0 ; i < 10 ; i++ )
    {
        pthread_signal_handler ( info_ptr );
        if ( daemon_is_file_present ( datafile.data() ))
        {
            return true ;
        }
        info_ptr->progress++ ;
        sleep (1);
    }

    info_ptr->status_string = "command did not produce output file ; timeout" ;
    info_ptr->status = FAIL_FILE_ACCESS ;
    return false ;
}

/*****************************************************************************
 *
 * Name        : _parse_redfish_sensor_data_output_file
 * Description : Parse power and thermal sensor data
 * Parameters  : info_ptr              - thread info
                 sensor_group          - power & thermal group
                 datafile  - date file used for storing redfishtool comand response
                 samples       - sensor data index for _sample_list array.
 * Returns     : PASS if file access is OK
 *
 *****************************************************************************/

static int _parse_redfish_sensor_data_output_file( thread_info_type * info_ptr,
                                                   int                sensor_group,
                                                   string &           datafile,
                                                   int &              samples )
{
    FILE * _fp = fopen ( datafile.data(), "r" );

    if ( _fp )
    {
        struct stat st;
        char buffer[HWMON_MAX_BMC_DATA_BUF_SIZE];

        /* zero the buffer according to the file size */
        if( fstat(fileno(_fp), &st) != 0 || (st.st_size + 2) > HWMON_MAX_BMC_DATA_BUF_SIZE)
        {
            elog_t ("%s file size is abnormal or bigger than buffer size\n",
                        info_ptr->hostname.c_str());
            return FAIL ;
        }
        else
        {
            memset (buffer, 0, (st.st_size + 2) );
        }
        fread(buffer,(st.st_size + 2), 1, _fp);
        fclose(_fp);

        /* Debug Option - enable lane debug_bmgt3 = 8 and touch
         * /var/run/bmc/ipmitool/want_dated_sensor_data_files for ipmi
         * or
         * /var/run/bmc/redfishtool/want_dated_sensor_data_files for redfish
         *
         * ... to save ther current sensor read file with a dated extension
         *     so that a read history is maintained for debug purposes. */
        if(daemon_get_cfg_ptr()->debug_bmgmt&8)
            if ( daemon_is_file_present (WANT_DATED_REDFISH_SENSOR_DATA_FILES))
                 daemon_copy_file(info_ptr->hostname, datafile.data());

        switch (sensor_group)
        {
            case BMC_SENSOR_POWER_GROUP:
            {
                _parse_redfish_sensor_data( buffer, info_ptr, REDFISH_SENSOR_LABEL_VOLT,
                                        REDFISH_SENSOR_LABEL_VOLT_READING, samples);
                _parse_redfish_sensor_data( buffer, info_ptr, REDFISH_SENSOR_LABEL_POWER_REDUNDANCY,
                                        REDFISH_SENSOR_LABEL_REDUNDANCY_READING, samples);
                _parse_redfish_sensor_data( buffer, info_ptr, REDFISH_SENSOR_LABEL_POWER_SUPPLY,
                                        REDFISH_SENSOR_LABEL_POWER_SUPPLY_READING, samples);
                _parse_redfish_sensor_data( buffer, info_ptr, REDFISH_SENSOR_LABEL_POWER_CTRL,
                                        REDFISH_SENSOR_LABEL_POWER_CTRL_READING, samples);
                return PASS;
            }
            case BMC_SENSOR_THERMAL_GROUP:
            {
                _parse_redfish_sensor_data( buffer, info_ptr, REDFISH_SENSOR_LABEL_TEMP,
                                        REDFISH_SENSOR_LABEL_TEMP_READING, samples);
                _parse_redfish_sensor_data( buffer, info_ptr, REDFISH_SENSOR_LABEL_FANS,
                                        REDFISH_SENSOR_LABEL_FANS_READING, samples);
                return PASS;
            }
            default:
            {
                elog_t ("%s unsupported command failure\n",
                            info_ptr->hostname.c_str());
            }
        }
    }
    else
    {
        info_ptr->status = FAIL_FILE_ACCESS ;
        info_ptr->status_string = "failed to open sensor data file: ";
        info_ptr->status_string.append(datafile);
    }

    return FAIL ;
}

/*****************************************************************************
 *
 * Name        : hwmonThread_redfish
 * Purpose     : This thread used for sending redfishtool command
 * Description : hwmon thread main function
 *
 *****************************************************************************/

void * hwmonThread_redfish ( void * arg )
{
    int samples ;

    thread_info_type       * info_ptr  ;
    thread_extra_info_type * extra_ptr ;
    string datafile = "";

    /* Pointer Error Detection and Handling */
    if ( !arg )
    {
        slog ("*** redfishtool thread called with null arg pointer *** corruption\n");
        return NULL ;
    }

    /* cast pointers from arg */
    info_ptr  = (thread_info_type*)arg   ;
    extra_ptr = (thread_extra_info_type*)info_ptr->extra_info_ptr ;

    info_ptr->pw_file_fd = 0 ;

    /* allow the parent to confirm thread id */
    info_ptr->id = pthread_self() ;
    if ( extra_ptr == NULL )
    {
        info_ptr->status_string = "null 'extra info' pointer" ;
        info_ptr->status = FAIL_NULL_POINTER ;
        goto redfishtool_thread_done ;
    }

    /* Set cancellation option so that a delete operation
     * can kill this thread immediately */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

    /* the number of sensors learned */
    extra_ptr->samples = samples = 0 ;
    MEMSET_ZERO (_sample_list);

    switch ( info_ptr->command )
    {
        case BMC_THREAD_CMD__READ_SENSORS:
        {
            blog2_t ("%s read power sensors \n", info_ptr->log_prefix);
            if ( _redfishUtil_send_request( info_ptr, datafile,
                                            BMC_POWER_SENSOR_FILE_SUFFIX,
                                            REDFISHTOOL_READ_POWER_SENSORS_CMD ) == PASS )
            {
                /* look for the output data file */
                if( _wait_for_command_output(info_ptr, datafile) )
                {
                    _parse_redfish_sensor_data_output_file( info_ptr, BMC_SENSOR_POWER_GROUP,
                                                            datafile, samples );
                }
                else
                {
                    break ;
                }
            }

            blog2_t ("%s read thermal sensors \n", info_ptr->log_prefix);
            if (_redfishUtil_send_request( info_ptr, datafile,
                                           BMC_THERMAL_SENSOR_FILE_SUFFIX,
                                           REDFISHTOOL_READ_THERMAL_SENSORS_CMD ) == PASS )
            {
                /* look for the output data file */
                if( _wait_for_command_output(info_ptr, datafile) )
                {
                    _parse_redfish_sensor_data_output_file( info_ptr, BMC_SENSOR_THERMAL_GROUP,
                                                            datafile, samples );
                }
            }

            extra_ptr->samples = samples ;
            if ( samples == 0 )
            {
                info_ptr->status = FAIL_NO_DATA ;
                info_ptr->status_string = "no sensor data found";
            }
            else
            {
                info_ptr->status_string = "pass" ;
                info_ptr->status = PASS ;
            }
            break ;
        }
        case BMC_THREAD_CMD__POWER_STATUS:
        {
            blog2_t ("%s read power state\n", info_ptr->log_prefix);
            if ( _redfishUtil_send_request( info_ptr, datafile,
                                            BMC_POWER_STATUS_FILE_SUFFIX,
                                            REDFISHTOOL_BMC_INFO_CMD ) != PASS )
            {
                info_ptr->status_string = "failed to send request" ;
                info_ptr->status = FAIL_OPERATION ;
                goto redfishtool_thread_done ;
            }
            /* look for the output data file */
            if( _wait_for_command_output(info_ptr, datafile) )
            {
               /* need to add one of the following 2 strings
                * to info_ptr->data
                *     - Chassis Power is on
                *     - Chassis Power is off
                */
                if ( datafile.empty() )
                {
                    info_ptr->status_string = "bmc info filename empty" ;
                    info_ptr->status = FAIL_NO_DATA ;
                    goto redfishtool_thread_done ;
                }

                /* read the output data */
                string json_bmc_info = daemon_read_file (datafile.data());
                if ( json_bmc_info.empty() )
                {
                    info_ptr->status_string = "bmc info file empty" ;
                    info_ptr->status = FAIL_STRING_EMPTY ;
                    goto redfishtool_thread_done ;
                }

                /* parse the output data */
                struct json_object *json_obj =
                    json_tokener_parse((char*)json_bmc_info.data());
                if ( !json_obj )
                {
                    info_ptr->status_string = "bmc info data parse error" ;
                    info_ptr->status = FAIL_JSON_PARSE ;
                    goto redfishtool_thread_done ;
                }

                /* load the power state */
                string power_state = tolowercase(
                jsonUtil_get_key_value_string( json_obj, REDFISH_LABEL__POWER_STATE));
                if ( power_state == "on" )
                {
                    info_ptr->data = "Chassis Power is on" ;
                }
                else
                {
                    info_ptr->data = "Chassis Power is off" ;
                }
                info_ptr->status_string = "pass" ;
                info_ptr->status = PASS ;
                ilog_t ("%s %s", info_ptr->hostname.c_str(),
                                 info_ptr->data.c_str());
                json_object_put( json_obj );
            }
            break ;
        }
        default:
        {
            info_ptr->status = FAIL_BAD_CASE ;
            _command_not_supported ( info_ptr );
            break ;
        }
    }

redfishtool_thread_done:

    if ( info_ptr->pw_file_fd > 0 )
        close(info_ptr->pw_file_fd);
    info_ptr->pw_file_fd = 0 ;

    if ( ! info_ptr->password_file.empty() )
    {
        unlink(info_ptr->password_file.data());
        daemon_remove_file ( info_ptr->password_file.data() ) ;
        info_ptr->password_file.clear();
    }

    pthread_signal_handler ( info_ptr );

    /* Sensor reading specific exit */
    if ( info_ptr->command == BMC_THREAD_CMD__READ_SENSORS )
    {
        _parse_sensor_data ( info_ptr );
    }

    info_ptr->progress++ ;
    info_ptr->runcount++ ;
    info_ptr->id = 0     ;
    pthread_exit (&info_ptr->status );
    return NULL ;
}

/*****************************************************************************
 *
 * Name        : hwmonThread_bmc
 * Purpose     : This thread used for sending bmc command
 * Description : hwmon thread main function
 *
 *****************************************************************************/

void * hwmonThread_bmc ( void * arg )
{
    string power_status = "";
    string protocol = "";

    /* cast pointers from arg */
    thread_info_type * info_ptr  = (thread_info_type*)arg   ;

    if (info_ptr->proto == BMC_PROTOCOL__REDFISHTOOL )
    {
        hwmonThread_redfish ( arg );
    }
    else
    {
        hwmonThread_ipmitool ( arg );
    }

    return NULL ;
}
