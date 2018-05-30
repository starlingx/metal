
/*
 * Copyright (c) 2016-2017 Wind River Systems, Inc.
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

using namespace std;

#include "daemon_common.h"

#include "nodeBase.h"
#include "nodeBase.h"        /* for ... mtce node common definitions     */
#include "hostUtil.h"        /* for ... mtce host common definitions     */
#include "nodeMacro.h"
#include "ipmiUtil.h"
#include "threadUtil.h"
#include "hwmonThreads.h"    /* for ... IPMITOOL_THREAD_CMD__READ_SENSORS */
#include "hwmonIpmi.h"       /* for ... MAX_IPMITOOL_PARSE_ERRORS         */
#include "hwmonClass.h"      /* for ... thread_extra_info_type            */

/***************************************************************************
 *
 * Name       : ipmitool_sample_type
 *
 * Description: An array of sensor data.
 *
 *    _sample_list
 *
 ***************************************************************************/

static ipmitool_sample_type   _sample_list[MAX_HOST_SENSORS] ;

/***************************************************************************
 *
 *               P R I V A T E       I N T E R F A C E S
 *
 **************************************************************************/

static void _command_not_supported ( thread_info_type * info_ptr )
{
    info_ptr->data = "{\"" ;
    info_ptr->data.append(IPMITOOL_JSON__SENSOR_DATA_MESSAGE_HEADER);
    info_ptr->data.append("\":{");
    info_ptr->data.append("\"status\":");
    info_ptr->data.append(itos(info_ptr->status));
    info_ptr->data.append(",");
    info_ptr->data.append("\"status_string\":\"command '");
    info_ptr->data.append(itos(info_ptr->command));
    info_ptr->data.append("' not supported\"}}");

    wlog_t ("%s %s\n", info_ptr->log_prefix, info_ptr->data.c_str());
}


static void _add_json_sensor_tuple ( ipmitool_sample_type * ptr, string & response )
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
        info_ptr->data.append (IPMITOOL_JSON__SENSOR_DATA_MESSAGE_HEADER);
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
        info_ptr->data.append (IPMITOOL_JSON__SENSORS_LABEL);
        info_ptr->data.append ("\":[");
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

void _get_field ( char * src_ptr , int field, char * dst_ptr )
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
    ++dst_ptr = '\0' ;

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
    switch ( info_ptr->command )
    {
        case IPMITOOL_THREAD_CMD__POWER_STATUS:
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
            ipmiUtil_create_pw_fn ( info_ptr, extra_ptr->bm_pw ) ;
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
            string ipmitool_datafile =
            ipmiUtil_create_data_fn (info_ptr->hostname, IPMITOOL_POWER_STATUS_FILE_SUFFIX ) ;
            dlog_t ("%s power query filename  : %s\n",
                        info_ptr->log_prefix,
                        ipmitool_datafile.c_str());

            /************** Create the ipmitool request **************/
            string ipmitool_request =
            ipmiUtil_create_request ( command,
                                      extra_ptr->bm_ip,
                                      extra_ptr->bm_un,
                                      info_ptr->password_file,
                                      ipmitool_datafile );
            dlog_t ("%s power status query cmd: %s\n",
                        info_ptr->log_prefix,
                        ipmitool_request.c_str());

            if ( daemon_is_file_present ( MTC_CMD_FIT__POWER_STATUS ))
            {
                slog ("%s FIT IPMITOOL_POWER_STATUS_CMD\n", info_ptr->hostname.c_str());
                rc = PASS ;
            }
            else
            {
                /* Make the request */
                rc = system ( ipmitool_request.data()) ;
            }

            unlink(info_ptr->password_file.data());
            daemon_remove_file (info_ptr->password_file.data());

            /* check for system call error case */
            if ( rc != PASS )
            {
                info_ptr->status_string = "failed power status query ; " ;
                info_ptr->status_string.append(ipmitool_request);
                info_ptr->status = FAIL_SYSTEM_CALL ;
            }
            else
            {
                bool ipmitool_datafile_present = false ;

                /* look for the output data file */
                for ( int i = 0 ; i < 10 ; i++ )
                {
                    pthread_signal_handler ( info_ptr );
                    if ( daemon_is_file_present ( ipmitool_datafile.data() ))
                    {
                        ipmitool_datafile_present = true ;
                        break ;
                    }
                    info_ptr->progress++ ;
                    sleep (1);
                }

                if ( ipmitool_datafile_present )
                {
                    info_ptr->data = daemon_read_file (ipmitool_datafile.data()) ;
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
        case IPMITOOL_THREAD_CMD__READ_SENSORS:
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

            ipmiUtil_create_pw_fn ( info_ptr, extra_ptr->bm_pw ) ;
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
            ipmiUtil_create_data_fn (info_ptr->hostname, IPMITOOL_SENSOR_OUTPUT_FILE_SUFFIX ) ;

            dlog_t ("%s sensor output file%s\n",
                      info_ptr->log_prefix,
                      sensor_datafile.c_str());

            /************** Create the ipmitool request **************/
            string sensor_query_request =
            ipmiUtil_create_request ( IPMITOOL_SENSOR_QUERY_CMD,
                                      extra_ptr->bm_ip,
                                      extra_ptr->bm_un,
                                      info_ptr->password_file,
                                      sensor_datafile );

            dlog_t ("%s sensor query cmd:%s\n",
                  info_ptr->log_prefix,
                  sensor_query_request.c_str());


            /****************************************************************
             *
             * This fault insertion case is added for PV.
             * If MTC_CMD_FIT__SENSOR_DATA file is present then no ipmitool
             * sensor read is performed. Instead, a raw output file can be
             * placed in /var/run/fit/<hostname>_sensor_data and used to
             * perform sensor fault insertion that way.
             *
             *****************************************************************/
            if ( daemon_is_file_present ( MTC_CMD_FIT__SENSOR_DATA ))
            {
                rc = PASS ;
            }
#ifdef WANT_FIT_TESTING
            else if ( daemon_want_fit ( FIT_CODE__HWMON__AVOID_SENSOR_QUERY, info_ptr->hostname ))
            {
                rc = PASS ; // ilog ("%s FIT Avoiding Sensor Query\n", info_ptr->hostname.c_str());
            }
            else if ( daemon_want_fit ( FIT_CODE__AVOID_N_FAIL_IPMITOOL_REQUEST, info_ptr->hostname ))
            {
                rc = FAIL ; // ilog ("%s FIT Avoiding Sensor Query\n", info_ptr->hostname.c_str());
            }
#endif
            else
            {
                /* remove the last query */
                // daemon_remove_file ( sensor_datafile.data() ) ;
                rc = system ( sensor_query_request.data()) ;
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
            // info_ptr->password_file.clear();

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
                            while (( buffer[i+1] != '\0' ) && ( i < IPMITOOL_MAX_LINE_LEN ))
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

                                    _get_field ( buffer, 0, _sample_list[samples].name   );
                                    _get_field ( buffer, 1, _sample_list[samples].value  );

                                    /* copy already learned type to unit field 2 */
                                    snprintf ( _sample_list[samples].unit, strlen(type)+1, "%s", type );

                                    _get_field ( buffer, 3, _sample_list[samples].status );
                                    _get_field ( buffer, 4, _sample_list[samples].lnr );
                                    _get_field ( buffer, 5, _sample_list[samples].lcr );
                                    _get_field ( buffer, 6, _sample_list[samples].lnc );
                                    _get_field ( buffer, 7, _sample_list[samples].unc );
                                    _get_field ( buffer, 8, _sample_list[samples].ucr );
                                    _get_field ( buffer, 9, _sample_list[samples].unr );
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
                    info_ptr->status_string = "failed to open sensor data file: <";
                    info_ptr->status_string.append(sensor_datafile);
                    info_ptr->status_string.append(">");
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
    if ( info_ptr->command == IPMITOOL_THREAD_CMD__READ_SENSORS )
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
