/*
 * Copyright (c) 2013-2019, 2025 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGTS Platform Maintenance ...
  *
  * /var/run/<daemon>.pid
  *
  * Also upon request can create a health check info file.
  *
  * /var/run/<daemon>.info
  *
  */

#include <stdlib.h>     /* for .. system           */
#include <unistd.h>     /* for .. close and usleep */
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>
#include <errno.h>
#include <openssl/md5.h>

using namespace std;

#include "daemon_common.h"
#include "daemon_option.h"
#include "nodeBase.h"
#include "nodeUtil.h"

/* GNU Extension
 * program_invocation_name contains the name that was used to invoke the
 * calling program. This is the same as the value of argv[0] in main(),
 * with the difference that the scope of program_invocation_name is global.
 * program_invocation_short_name contains the basename component of name
 * that was used to invoke the calling program. That is, it is the same
 * value as program_invocation_name, with all text up to and including
 * the final slash (/), if any, removed.
 * These variables are automatically initialized by the glibc run-time
 * startup code.
 *
 * The daemon name info */
extern char *program_invocation_name;
extern char *program_invocation_short_name;

static char pid_filename  [MAX_FILENAME_LEN] ;
static char hc_filename   [MAX_FILENAME_LEN] ;

#define BUFFER 1024

void daemon_files_fini ( void )
{
    close_syslog();
}

void daemon_health_test ( void )
{
    char file_str[2000] ;
    sprintf ( &file_str[0], "I'm healthy: Here is my config ...\n%s\n", daemon_stream_info ());
    daemon_healthcheck (&file_str[0]);
}

bool daemon_is_os_debian ( void )
{
    #define OS_RELEASE_FILE ((const char *)("/etc/os-release"))

    char   buffer   [BUFFER];
    char   line     [BUFFER];

    memset ( line, 0, BUFFER );

    FILE * file_stream = fopen ( OS_RELEASE_FILE, "r" );
    if ( file_stream != NULL )
    {
        int rc ;
        while ( fgets (buffer, BUFFER, file_stream) != NULL )
        {
            rc = sscanf ( &buffer[0], "ID=%1023s",  &line[0] );
            if ( rc == 1 )
            {
                string os = line ;
                if ( ! os.compare("debian") )
                {
                    dlog("%s:%s", OS_RELEASE_FILE, os.c_str());

                    /* Close the file stream */
                    fclose(file_stream);

                    return ( true ) ;
                }
            }
            else
            {
                dlog1 ("%s:%s", OS_RELEASE_FILE, &line[0]);
            }
        }
        /* Close the file stream */
        fclose(file_stream);
    }
    else
    {
        elog ("failed to open %s", OS_RELEASE_FILE);
    }
    return ( false );
}

bool daemon_is_file_present ( const char * filename )
{
    struct stat p ;
    memset ( &p, 0 , sizeof(struct stat));
    stat ( filename, &p ) ;
    if ((p.st_ino != 0 ) && (p.st_dev != 0))
        return (true);
    else
        return (false);
}

static void get_current_date_as_string ( char *date_str, size_t size )
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // Suffix Format: _YYYY-MM-DD_HH-MM-SS
    strftime(date_str, size, "%Y-%m-%d_%H-%M-%S", t);
}

// 16KB buffer for efficient copying
#define MAX_FILE_CONTENT_BUFFER_SIZE 0x4000 // 16 KBytes
int daemon_copy_file ( string hostname, const char *source )
{
    // Open source file in binary mode
    FILE *src = fopen (source, "rb");
    if (!src)
    {
        // Error path
        wlog ("%s unable to open source file: %s ; (%d:%m)", hostname.c_str(), source, errno);
        return FAIL ;
    }

    // Generate the destination filename with date suffix

    // Format: _YYYY-MM-DD_HH-MM-SS needs 21 chars with null termination
    char date_suffix[21];
    get_current_date_as_string (date_suffix, sizeof(date_suffix));

    // Max hostname size is 256 plus extra for path and the rest
    // of the filename size with the dated suffix. 512 is ample.
    // Example /var/run/bmc/redfishtool/hwmond_controller-0_thermal_sensor_data
    char destination[512];

    // Create the date suffixed destination filename
    snprintf(destination, sizeof(destination), "%s_%s", source, date_suffix);

    // Open destination file in binary mode
    FILE *dest = fopen(destination, "wb");
    if (!dest)
    {
        // Error path
        wlog ("%s failed to open destination file '%s' for copy operation",
                  hostname.c_str(), destination);

        fclose (src);
        return FAIL ;
    }

    char buffer [MAX_FILE_CONTENT_BUFFER_SIZE];
    size_t bytes_read;

    // Read from source and write to destination in chunks
    while ((bytes_read = fread(buffer, 1, MAX_FILE_CONTENT_BUFFER_SIZE, src)) > 0)
        fwrite(buffer, 1, bytes_read, dest);

    // Close files
    fclose (src);
    fclose (dest);

    ilog ("%s file '%s' copied to '%s'\n", hostname.c_str(), source, destination);
    return PASS ;
}

void daemon_healthcheck ( const char * sig )
{
    FILE * hc_file_stream ;

    snprintf ( &hc_filename[0], MAX_FILENAME_LEN, "/var/run/%s.info",
                program_invocation_short_name );

    /* remove the old file */
    unlink (hc_filename);

    /* Create daemon healthcheck file */
    hc_file_stream = fopen (hc_filename, "w" ) ;
    if ( hc_file_stream == NULL )
    {
        wlog("Failed to open %s\n", hc_filename);
    }
    else if ( !fprintf (hc_file_stream,"%s\n", sig ))
    {
        wlog ("Failed to write to %s\n", hc_filename);
    }
    if ( hc_file_stream )
    {
        fflush (hc_file_stream);
        fclose (hc_file_stream);
    }
}


int daemon_log_value ( const char * filename , const char * str, int val )
{
    FILE * file_stream = fopen (filename, "a" ) ;
    if ( file_stream != NULL )
    {
        fprintf ( file_stream,"%s %d\n", str, val );
        fflush (file_stream);
        fclose (file_stream);
        return (PASS);
    }
    return (FAIL_FILE_OPEN);
}

int daemon_log_value ( const char * filename , int val )
{
    FILE * file_stream = fopen (filename, "w" ) ;
    if ( file_stream != NULL )
    {
        fprintf ( file_stream,"%d\n", val );
        fflush (file_stream);
        fclose (file_stream);
        return (PASS);
    }
    return (FAIL_FILE_OPEN);
}

int daemon_log ( const char * filename , const char * str )
{
    FILE * file_stream = fopen (filename, "a" ) ;
    if ( file_stream != NULL )
    {
        fprintf ( file_stream,"%s\n", str );
        fflush (file_stream);
        fclose (file_stream);
        return (PASS);
    }
    return (FAIL_FILE_OPEN);
}

/* reads the first line of a file and if it contains a string
 * that represents an integer value then return it */
int daemon_get_file_int ( const char * filename )
{
    int    value = 0 ;
    FILE * __stream = fopen ( filename, "r" );
    if ( __stream != NULL )
    {
        int rc ;

        char   buffer     [MAX_CHARS_IN_INT];
        memset(buffer, 0 , MAX_CHARS_IN_INT);
        if ( fgets (buffer,MAX_CHARS_IN_INT, __stream) != NULL )
        {
            rc = sscanf ( &buffer[0], "%d",  &value );
            if ( rc >= 1 )
            {
                dlog ("%s contains number %d\n", filename, value );
            }
            else
            {
                wlog ("failed to sscanf integer from file:%s\n", filename );
            }
        }
        else
        {
            wlog ("failed to read integer from file:%s\n", filename );
        }
        fclose(__stream);
    }
    else
    {
        wlog ("failed to open file:%s\n", filename );
    }
    return ( value );
}

/* reads the first line of a file and returns it as a string */
string daemon_get_file_str ( const char * filename )
{
    string  value = "" ;
    FILE * __stream = fopen ( filename, "r" );
    if ( __stream != NULL )
    {
        char   buffer     [MAX_CHARS_ON_LINE];
        memset(buffer, 0 , MAX_CHARS_ON_LINE);
        if ( fgets (buffer,MAX_CHARS_ON_LINE, __stream) != NULL )
        {
            int len = strlen(buffer) ;
            if ( len )
            {
                /* strip of a newline if that's the last character */
                if ( buffer[len-1] == '\n' )
                    buffer[len-1] = '\0' ;
                value = buffer ;
                dlog("'%s' read from %s", value.c_str(), filename);
            }
            else
            {
                wlog("no string data in %s", filename);
            }
        }
        fclose(__stream);
    }
    else
    {
        wlog ("failed to open file:%s\n", filename );
    }
    return ( value );
}

/* Stay here till we get the data we need */
/* Warning: Don't enable logging here     */
string daemon_nodetype ( void )
{
    for ( ; ; )
    {
        char   buffer[BUFFER];
        int    line = 0      ;
        string nodetype = "unknown" ;
        char   nodetype_str[BUFFER];

        memset ( &nodetype_str[0], 0 , BUFFER );
        /* open the configuration file */
        FILE * cfg_file_stream = fopen ( PLATFORM_CONF_FILE, "rb" );
        if ( cfg_file_stream != NULL )
        {
            int rc ;
            while ( fgets (buffer, BUFFER, cfg_file_stream) != NULL )
            {
                char* s = strstr ( buffer, "subfunction");
                if(s!=NULL)
                {
                    rc = sscanf ( &buffer[0], "subfunction=%1023s",  &nodetype_str[0] );
                    if ( rc == 1 )
                    {
                        nodetype = nodetype_str ;
                        fclose(cfg_file_stream);
                        return ( nodetype ) ;
                    }
                }
                line++ ;
            }
            /* Close the file */
            fclose(cfg_file_stream);
        }
        usleep (5000000);
    }
    return ( "" );
}

/* Read the integer value of rmem_max from /proc/sys/net/core/rmem_max */
int daemon_get_rmem_max ( void )
{
    #define RMEM_MAX_VALUE_SIZE (64)
    #define RMEM_MAX_FILENAME ((const char *)("/proc/sys/net/core/rmem_max"))

    int    value = 0 ;
    FILE * __stream = fopen ( RMEM_MAX_FILENAME, "r" );
    if ( __stream != NULL )
    {
        int rc ;

        char   buffer     [RMEM_MAX_VALUE_SIZE];
        memset(buffer, 0 , RMEM_MAX_VALUE_SIZE);
        if ( fgets (buffer,RMEM_MAX_VALUE_SIZE, __stream) != NULL )
        {
            rc = sscanf ( &buffer[0], "%d",  &value );
            if ( rc == 1 )
            {
                dlog1 ("mem_max:%d\n", value );
            }
        }
        fclose(__stream);
    }
    return ( value );
}

/*
 * Read the platform.conf file looking for the management_interface label.
 * If found return that value as string ; otherwise return empty string.
 *
 *  const char clstr_mgmt_label [] = {"management_interface"} ;
 *
 * Stay here till we get the data we need
 * Warning: Don't enable logging here
 *
 **/
string daemon_mgmnt_iface ( void )
{
    for ( ; ; )
    {
        char   iface_str[BUFFER];
        char   buffer   [BUFFER];
        int    line  = 0        ;
        string iface = ""       ;

        memset ( iface_str, 0 , BUFFER );
        FILE * cfg_file_stream = fopen ( PLATFORM_CONF_FILE, "r" );
        if ( cfg_file_stream != NULL )
        {
            int rc ;
            while ( fgets (buffer, BUFFER, cfg_file_stream) != NULL )
            {
                char* s = strstr ( buffer, "management_interface");
                if(s!=NULL)
                {
                    rc = sscanf ( &buffer[0], "management_interface=%1023s",  &iface_str[0] );
                    if ( rc == 1 )
                    {
                        iface = iface_str ;
                        fclose(cfg_file_stream);
                        // ilog("Mgmnt iface : %s\n", iface.c_str() );
                        return ( iface ) ;
                    }
                }
                line++ ;
            }
            /* Close the file */
            fclose(cfg_file_stream);
        }
        usleep (5000000);
    }
    ilog("Mgmnt iface : none\n");
    return ( "" );
}


/****************************************************************************
 *
 * Name       : daemon_system_type
 *
 * Purpose    : Learn the system type
 *
 * Description: Read the platform.conf file looking for system type.
 *              If type is not Standard then refine the system type to
 *              one of the AIO variations.
 *
 *****************************************************************************/

#define SYSTEM_TYPE_PREFIX ((const char *)("System Type :"))
system_type_enum daemon_system_type ( void )
{
    char buffer  [BUFFER];
    system_type_enum system_type = SYSTEM_TYPE__AIO__SIMPLEX ;
    FILE * cfg_file_stream = fopen ( PLATFORM_CONF_FILE, "r" );
    if ( cfg_file_stream != NULL )
    {
        MEMSET_ZERO(buffer);
        while ( fgets (buffer, BUFFER, cfg_file_stream) != NULL )
        {
            if ( strstr ( buffer, "system_type") != NULL )
            {
                char   type_str[BUFFER];
                MEMSET_ZERO(type_str);
                if ( sscanf ( &buffer[0], "system_type=%1023s",  &type_str[0] ) == 1 )
                {
                    string type_string = type_str ;
                    type_string = tolowercase(type_string);
                    if ( !type_string.empty() && ( type_string == "standard"))
                    {
                        system_type = SYSTEM_TYPE__NORMAL ;
                    }
                    break ;
                }
            }
            MEMSET_ZERO(buffer);
        }
    }

    if ( cfg_file_stream )
    {
        /* Close the file */
        fclose(cfg_file_stream);
        cfg_file_stream = NULL ;
    }

    /* If system_type is updated to NORMAL then we are done.
     * Otherwise lets see what kind of AIO system mode we are running in. */
    if ( system_type != SYSTEM_TYPE__NORMAL )
    {
        cfg_file_stream = fopen ( PLATFORM_CONF_FILE, "r" );
        if ( cfg_file_stream != NULL )
        {
            MEMSET_ZERO(buffer);
            while ( fgets (buffer, BUFFER, cfg_file_stream) != NULL )
            {
                if ( strstr ( buffer, "system_mode") != NULL )
                {
                    char mode_str[BUFFER];
                    MEMSET_ZERO(mode_str);
                    if ( sscanf ( &buffer[0], "system_mode=%1023s",  &mode_str[0] ) == 1 )
                    {
                        string mode = mode_str ;
                        mode = tolowercase(mode);
                        if ( !mode.empty() )
                        {
                            if ( mode.compare("duplex") == 0 )
                                system_type = SYSTEM_TYPE__AIO__DUPLEX ;
                            else if ( mode.compare("duplex-direct") == 0 )
                                system_type = SYSTEM_TYPE__AIO__DUPLEX_DIRECT ;
                            else if ( mode.compare("simplex") == 0 )
                                system_type = SYSTEM_TYPE__AIO__SIMPLEX ;
                            else
                            {
                                elog ("%s All-In-One system type ; mode unknown\n", SYSTEM_TYPE_PREFIX );
                                wlog ("... %s\n", buffer );
                            }
                        }
                        else
                        {
                            elog ("%s All-In-One system type ; mode empty\n", SYSTEM_TYPE_PREFIX );
                            wlog ("... %s\n", buffer );
                        }
                    }
                    break ;
                }
                MEMSET_ZERO(buffer);
            }
        }
    }

    if ( cfg_file_stream )
    {
        /* Close the file */
        fclose(cfg_file_stream);
    }

    switch ( system_type )
    {
        case SYSTEM_TYPE__NORMAL:
        {
            ilog("%s Standard System\n", SYSTEM_TYPE_PREFIX);
            break ;
        }
        case SYSTEM_TYPE__AIO__DUPLEX_DIRECT:
        {
            ilog ("%s All-in-one Duplex Direct Connect\n", SYSTEM_TYPE_PREFIX );
            break ;
        }
        case SYSTEM_TYPE__AIO__DUPLEX:
        {
            ilog ("%s All-in-one Duplex\n", SYSTEM_TYPE_PREFIX );
            break ;
        }
        case SYSTEM_TYPE__AIO__SIMPLEX:
        default:
        {
            ilog ("%s All-in-one Simplex \n", SYSTEM_TYPE_PREFIX );
            system_type = SYSTEM_TYPE__AIO__SIMPLEX ;
            break ;
        }
    }
    return ( system_type );
}


/* ********************************************************************
 *
 * Name:        daemon_clstr_iface
 *
 * Description: Read the platform.conf file looking for the cluster-host
 *              interface label. If found return that value as string ;
 *              otherwise return empty string.
 **/
string daemon_clstr_iface ( void )
{
    char   buffer   [BUFFER];
    int    line  = 0        ;
    string iface = ""       ;

    FILE * cfg_file_stream = fopen ( PLATFORM_CONF_FILE, "r" );
    if ( cfg_file_stream != NULL )
    {
        int rc ;
        while ( fgets (buffer, BUFFER, cfg_file_stream) != NULL )
        {
            char* s = strstr ( buffer, "cluster_host_interface" );
            if(s!=NULL)
            {
                char iface_str[BUFFER];
                memset ( iface_str, 0 , BUFFER );
                rc = sscanf ( &buffer[0], "cluster_host_interface=%1023s", &iface_str[0] );
                if ( rc == 1 )
                {
                    iface = iface_str ;
                    fclose(cfg_file_stream);
                    // ilog("Cluster-host iface : %s\n", iface.c_str() );
                    return ( iface ) ;
                }
            }
            line++ ;
        }
        /* Close the file */
        fclose(cfg_file_stream);
    }
    dlog("Cluster-host iface : none\n");
    return ( "" );
}

/* *********************************************************************************
 *
 * Name       : daemon_sw_version
 *
 * Description: read the /etc/build.info file and extract the SW_VERSION value.
 *
 * Note: If the value is found surrounded by quotes then they are stripped.
 * Note: if the lable is not found then a empty string is returned.
 *
 * @return SW_VERSION string value
 *
 * Here is the head of the file with that label.
 *
 * root@controller-0:/opt# cat /etc/build.info
 *
 * SW_VERSION="14.08"    <-----------------------
 * BUILD_TARGET="Unknown"
 * BUILD_TYPE="Informal"
 * BUILD_ID="n/a"
 *
 ************************************************************************************/

string daemon_sw_version ( void )
{
    char   version_str[BUFFER];
    string version = "" ;
    int    line    = 0  ;

    const char build_info_filename[] = { "/etc/build.info" };
    FILE * fp = fopen (&build_info_filename[0], "r" ) ;
    if ( fp )
    {
        int   rc  = 0 ;
        char buffer[BUFFER];
        while ( fgets (buffer, BUFFER, fp ) != NULL )
        {
            memset ( version_str, 0 , BUFFER );
            char* s = strstr ( buffer, "SW_VERSION");
            if(s!=NULL)
            {
                rc = sscanf ( &buffer[0], "SW_VERSION=%1023s", &version_str[0] );
                if ( rc == 1 )
                {
                    /* Chop off the surrounding quotes if they exist */
                    if ( version_str[0] == '"' )
                    {
                        /* "12.34" -> 12.34 */
                        int len = strlen (version_str);
                        string temp = version_str ;
                        version = temp.substr ( 1, len-2 ) ;
                    }
                    else
                    {
                        version = version_str ;
                    }
                    fclose(fp);
                    return ( version ) ;
                }
            }
            line++ ;
        }
        /* Close the file */
        fclose(fp);
    }
    return ("");
}

/****************************************************************************
 *
 * Name       : daemon_bmc_hosts_file
 *
 * Description: Insert the software version into the path to the bmc hosts
 *              file and return the file path as a string.
 *
 ****************************************************************************/
string daemon_bmc_hosts_dir ( void )
{
    string fn = "/opt/platform/config/" ;
    string version = daemon_sw_version();
    if ( !version.empty() )
    {
         version.append("/");
    }
    fn.append(version);
    return(fn);
}
string daemon_bmc_hosts_file ( void )
{
    string fn = "/opt/platform/config/" ;
    string version = daemon_sw_version();
    if ( !version.empty() )
    {
         version.append("/");
    }
    fn.append(version);
    fn.append(BM_DNSMASQ_FILENAME);
    return(fn);
}

/****************************************************************************
 *
 * Name       : daemon_get_iface_master
 *
 * Description: Get the master interface name for the supplied
 *              physical interface.
 *
 ****************************************************************************/
char master_interface [BUFFER] ;
char * daemon_get_iface_master ( char * iface_slave_ptr )
{
    FILE * file_ptr ;
    char   buffer[BUFFER];

    string iface_master = "/sys/class/net/" ;
    iface_master.append (iface_slave_ptr) ;
    iface_master.append ("/master/uevent");

    /* Create daemon healthcheck file */
    file_ptr = fopen ( iface_master.data(), "r" ) ;
    if ( file_ptr != NULL )
    {
        while ( fgets ( buffer, BUFFER, file_ptr ) != NULL )
        {
            char* s = strstr ( buffer, "INTERFACE");
            if ( s != NULL )
            {
                memset ( &master_interface[0], 0, BUFFER );
                int rc = sscanf ( &buffer[0], "INTERFACE=%1023s",
                         &master_interface[0] );
                if ( rc == 1 )
                {
                    fclose(file_ptr);
                    return ( &master_interface[0] );
                }
            }
        }
        fclose(file_ptr);
    }
    return ( iface_slave_ptr );
}

string daemon_md5sum_file ( const char * file )
{
    struct stat p ;
    string md5sum = "" ;

    memset ( &p, 0 , sizeof(struct stat));
    stat ( file, &p ) ;
    if ((p.st_ino != 0 ) && (p.st_dev != 0))
    {
        /* add 256 bytes to the buffer just in case there are
         * additions to the file by the time we start reading it */
        int len = p.st_size+0x100 ;

        char * buf_ptr = (char*)malloc(len) ;
        char * buf_ptr_save = buf_ptr ;
        if ( buf_ptr )
        {
            dlog ("%s is %ld bytes\n", file, p.st_size );

            /* Open and read the file data */
            FILE * file_ptr = fopen ( file, "r" ) ;
            if ( file_ptr != NULL )
            {
                size_t l ;
                unsigned char digest[MD5_DIGEST_LENGTH];
                char md5str         [MD5_STRING_LENGTH];
                memset ( &digest, 0, MD5_DIGEST_LENGTH);
                memset ( &md5str, 0, MD5_STRING_LENGTH);

                while ( fgets ( (char*)buf_ptr, BUFFER, file_ptr ) != NULL )
                {
                    l = strnlen ( buf_ptr_save, len );
                    buf_ptr = buf_ptr_save+l ;
                }
                MD5 ( (unsigned char*)buf_ptr_save, strlen(buf_ptr_save), (unsigned char*)&digest);

                for(int i = 0; i < MD5_DIGEST_LENGTH; i++)
                    sprintf(&md5str[i*2], "%02x", (unsigned int)digest[i]);

                dlog ("md5 digest: %s\n", md5str );
                dlog3 ("file: %s\n", buf_ptr_save );

                fclose(file_ptr);
                md5sum = md5str ;
            }
            else
            {
                elog ("%s file open failed\n", file );
            }
            free (buf_ptr_save);
        }
        else
        {
            elog ("failed to allocate buffer memory for %s file md5sum calc\n", file );
        }
    }
    else
    {
        wlog ("%s file not present\n", file );
    }
    return ( md5sum );
}

// generate a md5sum signature for the Shadow entry
// returns the password hash and aging in the shadowinfo
string get_shadow_signature ( char * shadowfile , const char * username,
                              char *shadowinfo, size_t infolen)
{
    char buffer[BUFFER];

    /* Open the specified file file Create daemon healthcheck file */
    FILE * file_ptr = fopen ( shadowfile, "r" ) ;

    if ( file_ptr != NULL )
    {
        /* Clear the buffer - start fresh */
        memset ( &buffer, 0, BUFFER );

        while ( fgets ( buffer, BUFFER, file_ptr ) != NULL )
        {
            char* s = strstr ( buffer, username);
            if ( s != NULL )
            {
                int result;
                char user[BUFFER], password[BUFFER], aging[BUFFER];
                unsigned char digest[MD5_DIGEST_LENGTH];
                char md5str         [MD5_STRING_LENGTH];

                /* Fields are separated by ':'.  The first field is the
                 * user. We need to only isolate the password and aging
                 * fields since these are the only ones that'd be
                 * propagated across host nodes. By specifically tracking
                 * these we prevent config-out-of-date alarms for other fields
                 */
                
                /*
                 * The following line should be changed to add width limits:
                 * (However, not changing it yet because of risk.)
                 * result = sscanf(buffer, "%1023[^:]:%1023[^:]:%*[^:]:%*[^:]:%1023[^:]",
                 */
                result = sscanf(buffer, "%[^:]:%[^:]:%*[^:]:%*[^:]:%[^:]",
                user, password, aging);
                if ( result != 3 || strcmp(user, username) != 0 )
                {
                    /* Sanity */
                    continue;
                }

                /* at max, both password[] and aging[] include BUFFER chars (BUFFER-1 
                 * meaningful chars and one "\0" as tail). when they are combined with
                 * ":" and put into shadowEntry by snprintf (..., "%s:%s", ...), 
                 * shadowEntry has 2 chars (":" + "\0") at least and BUFFER*2 chars at most:
                 *     BUFFER-1 chars copied from password
                 *     ":"
                 *     BUFFER-1 chars copied from aging, and 
                 *     one tail "\0"
                 */
                char shadowEntry[BUFFER*2] = {0};
                snprintf (shadowEntry, sizeof(shadowEntry), 
                          "%s:%s", password, aging);

                int ret = snprintf(shadowinfo, infolen, "%s", shadowEntry);
                if (ret >= (int)infolen)
                {
                    fclose(file_ptr);
                    elog("insufficient space in shadow buffer(%d) for %d bytes\n",
                         (int)infolen, ret);
                    return ( "" );
                }
    
                memset ( &digest, 0, MD5_DIGEST_LENGTH );
                memset ( &md5str, 0, MD5_STRING_LENGTH );
                MD5 ((unsigned char*) shadowEntry, strlen(shadowEntry), (unsigned char*)&digest);

                for(int i = 0; i < MD5_DIGEST_LENGTH; i++)
                    sprintf(&md5str[i*2], "%02x", (unsigned int)digest[i]);

                dlog ("user value: %s\n", shadowEntry);
                dlog ("md5 digest: %s\n", md5str );

                fclose(file_ptr);
                string md5sum = md5str ;
                return ( md5sum );
            }
            else
            {
                /* Clear the buffer - start fresh */
                memset ( &buffer, 0, BUFFER );
            }
        }
        fclose(file_ptr);
    }
    return ( "" );

}

/* Introduced for process monitor daemon to allow quiet
 * close of the log file before a process respawn */
void daemon_logfile_close ( void )
{
    return ;
}

/* Introduced for process monitor daemon to allow quiet
 * re-open of the log file after a respawn of a process */
void daemon_logfile_open  ( void )
{
    return ;
}

/****************************************************************************
 *
 * Name       : daemon_remove_file
 *
 * Description: Remove the specified file if it exists.
 *
 *****************************************************************************/

void daemon_remove_file ( const char * filename )
{
    if ( daemon_is_file_present ( filename ))
    {
        if ( remove(filename) )
        {
            elog ("failed to remove file '%s' ; (%d:%m)\n", filename, errno);
        }
        else
        {
            if ( daemon_is_file_present ( filename ) )
            {
                elog ("failed to remove file '%s' ; still present\n", filename );
            }
            else
            {
                dlog3 ("removed %s\n", filename );
            }
        }
    }
    else
    {
        dlog3 ("no remove required ; file '%s' not present\n", filename );
    }
}

/****************************************************************************
 *
 * Name       : daemon_make_dir
 *
 * Description: Create the specified full path directory.
 *
 *****************************************************************************/

void daemon_make_dir ( const char * dir )
{
    struct stat st ;
    MEMSET_ZERO(st);
    if (stat(dir, &st) == -1)
    {
        if ( mkdir (dir, 0755) != 0 )
        {
            elog ("failed to create directory '%s' ; (%d:%m)\n", dir, errno );
        }
    }
}

/****************************************************************************
 *
 * Name       : daemon_rename_file
 *
 * Description: Rename the specified file from old to new name.
 *
 * Warnings   : If the target rename filename exists then it is removed first.
 * 
 *****************************************************************************/

void daemon_rename_file ( const char * path, const char * old_filename, const char * new_filename )
{
    if ( path )
    {
        string _old = path ;
        _old.append("/");
        _old.append(old_filename);
        if ( daemon_is_file_present ( _old.data() ) == true )
        {
            string _new = path ;
            _new.append("/");
            _new.append(new_filename);

            if ( daemon_is_file_present ( _new.data() ) == true )
            {
                dlog ("removing '%s' prior to rename\n", _new.c_str());
                daemon_remove_file ( _new.data() );
            }
            if ( rename ( _old.data(), _new.data()) == 0)
	        {
		        dlog ("file rename : %s -> %s\n", old_filename, new_filename);
	        }
	        else
	        {
		        elog ( "Error renaming %s -> %s (%d:%m)\n", old_filename, new_filename, errno );
            }
        }
        else
        {
            elog ("rename failed ; '%s' not found\n", _old.c_str());
        }
	}
}

void daemon_remove_pidfile ( void )
{
    const char* cmd_str = "rm -f ";
    size_t cmd_len = sizeof(cmd_str);
    char str [MAX_FILENAME_LEN+cmd_len] ;
    snprintf (str, sizeof(str), "rm -f %s", pid_filename);
    int rc = system (str);
    if ( rc )
    {
       wlog ("system command to remove %s returned %d\n", &pid_filename[0], rc );
    }
}

int daemon_create_pidfile ( void )
{
    FILE * pid_file_stream  = (FILE *)(NULL);

    snprintf ( &pid_filename[0], MAX_FILENAME_LEN, "/var/run/%s.pid",
                program_invocation_short_name );

    /* Create PID file */
    pid_t mypid = getpid();

    /* Check for another instance running by trying to open in read only mode.
     * If it opens then there "may" be another process running.
     * If it opens then read the pid and see if that pID exists.
     * If it does then this is a duplicate process so exit. */
    pid_file_stream = fopen (&pid_filename[0], "r" ) ;
    if ( pid_file_stream )
    {
        int   rc  = 0 ;
        pid_t pid = 0 ;
        char buffer[BUFFER];
        if ( fgets ( buffer, BUFFER, pid_file_stream) != NULL )
        {
            rc = sscanf ( &buffer[0], "%d",  &pid );
            if ( rc == 1 )
            {
                rc = kill ( pid, 0 );
                if ( rc == 0 )
                {
                    syslog ( LOG_INFO, "Refusing to start duplicate process (pid:%d)\n", pid);
                    fclose (pid_file_stream);
                    daemon_files_fini ();
                    exit (0);
                }
            }
        }
    }

    if ( pid_file_stream )
        fclose (pid_file_stream);

    /* if we got here then we are ok to run */
    pid_file_stream = fopen (pid_filename, "w" ) ;

    if ( pid_file_stream == NULL )
    {
        syslog ( LOG_ERR, "Failed to open or create %s\n", pid_filename);
        return ( FAIL_PID_OPEN );
    }
    else if (!fprintf (pid_file_stream,"%d", mypid))
    {
        syslog ( LOG_ERR, "Failed to write pid file for %s\n", pid_filename );
        fclose ( pid_file_stream ) ;
        return ( FAIL_FILE_WRITE ) ;
    }
    fflush (pid_file_stream);
    fclose (pid_file_stream);
    return (PASS);
}

string daemon_read_file ( const char * filename )
{
    string data = "" ;
    int lines = 0 ;
    if ( daemon_is_file_present ( filename ) == true )
    {
        FILE * _stream = fopen ( filename, "r" );
        if ( _stream )
        {
            char buffer   [BUFFER];
            MEMSET_ZERO(buffer);
            while ( fgets (buffer, BUFFER, _stream) )
            {
                data.append(buffer);
                if ( ++lines > 5000 )
                {
                    wlog ("%s file to big ; aborting\n", filename );
                    break ;
                }
                MEMSET_ZERO(buffer);
            }
            fclose (_stream);
        }
    }
    return data ;
}

/***************************************************************************
 *
 * Don't return from this call until the specified file exists
 * or the timeout is exceeded. In the timeout case a FAIL_TIMEOUT
 * is returned.
 *
 * Warning: Timeout timer is not yet implemented
 *
 ***************************************************************************/
int daemon_wait_for_file ( const char * filename, int timeout )
{
    UNUSED(timeout) ;
    if ( filename )
    {
        int count = 0 ;
        for ( ; ; )
        {
            daemon_signal_hdlr ();
            if ( daemon_is_file_present( filename ))
                return PASS ;
            sleep (MTC_SECS_2);
            wlog_throttled ( count, 60, "Waiting for %s\n", filename);
        }
    }
    return (FAIL_TIMEOUT);
}

int daemon_files_init ( void )
{
    struct timespec ts ;
    clock_gettime (CLOCK_MONOTONIC, &ts );

    /* Create PID file */
    pid_t mypid = getpid();
    ilog   ("--- Daemon Start-Up --- pid:%d uptime:%ld", mypid, ts.tv_sec);
    daemon_init_fit ();
    return ( PASS );
}

/*****************************************************************************************
 *
 * #######  ###  #######      #####   #     #  ######   ######   #######  ######   #######
 * #         #      #        #     #  #     #  #     #  #     #  #     #  #     #     #
 * #         #      #        #        #     #  #     #  #     #  #     #  #     #     #
 * #####     #      #         #####   #     #  ######   ######   #     #  ######      #
 * #         #      #              #  #     #  #        #        #     #  #   #       #
 * #         #      #        #     #  #     #  #        #        #     #  #    #      #
 * #        ###     #         #####    #####   #        #        #######  #     #     #
 *
 *****************************************************************************************/

#ifdef WANT_FIT_TESTING
static daemon_fit_type __fit_info ;
#endif

#ifdef WANT_HIT_THROTTLE
int throttle_max   ;
int throttle_count ;
#endif

void daemon_print_fit ( void )
{
#ifdef WANT_FIT_TESTING
    if ( __fit_info.code )
    {
        if ( !daemon_get_cfg_ptr()->testmode )
        {
            slog ("%s FIT Mode Not Enabled ; need to enable testmode\n",
                      __fit_info.host.c_str());

            daemon_init_fit ( );
        }
        else if ( __fit_info.hits == 0 )
        {
            daemon_init_fit ();
        }
        else
        {
            slog ("%s FIT Add:%d '%s' with '%s' %d times\n",
                   __fit_info.host.empty() ? "any" : __fit_info.host.c_str(),
                   __fit_info.code,
                   __fit_info.name.empty() ? "n/a" : __fit_info.name.c_str(),
                   __fit_info.data.empty() ? "n/a" : __fit_info.data.c_str(),
                   __fit_info.hits);
        }
    }
#endif
}

void daemon_hits_fit ( int hits )
{
#ifdef WANT_FIT_TESTING
    __fit_info.hits += hits ;
    daemon_print_fit ();
#else
    UNUSED(hits);
#endif
}

void daemon_init_fit ( void )
{
#ifdef WANT_FIT_TESTING
    ilog ("FIT Inactive\n");
    __fit_info.code = 0 ;
    __fit_info.host.clear() ;
    __fit_info.name.clear() ;
    __fit_info.proc.clear() ;
    __fit_info.data.clear() ;
    __fit_info.hits = 0 ;

    /* Indicate that the fit is unloaded */
    if ( daemon_is_file_present ( "/var/run/fit") )
        daemon_log ( "/var/run/fit/fitdone", "done" );

#endif
}

/* called by processes that don't match the fit proc name */
void daemon_ignore_fit ( void )
{
#ifdef WANT_FIT_TESTING
    __fit_info.code = 0 ;
    __fit_info.host.clear() ;
    __fit_info.name.clear() ;
    __fit_info.proc.clear() ;
    __fit_info.data.clear() ;
    __fit_info.hits = 0 ;

#endif
}

void daemon_handle_hit ( void )
{
#ifdef WANT_FIT_TESTING
    if ( __fit_info.code )
    {
        --__fit_info.hits ;

#ifdef WANT_HIT_THROTTLE
        ilog_throttled (throttle_count, throttle_max, "%s FIT Hit:%d '%s' with '%s' %d times remaining\n",
               __fit_info.host.empty() ? "any" : __fit_info.host.c_str(),
               __fit_info.code,
               __fit_info.name.empty() ? "n/a" : __fit_info.name.c_str(),
               __fit_info.data.empty() ? "n/a" : __fit_info.data.c_str(),
               __fit_info.hits);
#else
        slog ("%s FIT Hit:%d '%s' with '%s' %d times remaining\n",
               __fit_info.host.empty() ? "any" : __fit_info.host.c_str(),
               __fit_info.code,
               __fit_info.name.empty() ? "n/a" : __fit_info.name.c_str(),
               __fit_info.data.empty() ? "n/a" : __fit_info.data.c_str(),
               __fit_info.hits);
#endif
        if ( __fit_info.hits == 0 )
            daemon_ignore_fit ();
        else
            daemon_log_value ( "/var/run/fit/fithits", "hits =", __fit_info.hits );
    }
#endif
}


/* Read the fit file if its present and load its fit info */
int  daemon_load_fit ( void )
{
#ifdef WANT_FIT_TESTING
    if ( __fit_info.code )
    {
        return (PASS);
    }

    if ( daemon_is_file_present ( FIT__INIT_FILE ) == true )
    {
        daemon_rename_file ( FIT__INIT_FILEPATH, FIT__INIT_FILENAME, FIT__INIT_FILENAME_RENAMED );
        daemon_init_fit ();
    }

    if ( daemon_is_file_present ( FIT__INFO_FILE ) == false ) return (PASS);

    bool correct_process = false ;
    bool valid_code = false ;

    FILE * _stream = fopen ( FIT__INFO_FILE, "r" );
    if ( _stream )
    {
        char buffer   [BUFFER];
        memset (buffer, 0 , BUFFER );
        while ( fgets (buffer, BUFFER, _stream) )
        {
            if (( correct_process == false ) &&
                ( strstr ( buffer, "proc=" )))
            {
                char _str[BUFFER];
                memset ( _str, 0 , BUFFER );
                int chars = 0 ;
                if ( ( chars = sscanf ( &buffer[0], "proc=%1023s", &_str[0] ) ) == 1 )
                {
                    string proc = program_invocation_short_name ;
                    if ( proc.compare(_str) == 0 )
                    {
                        __fit_info.proc = proc ;
                        correct_process = true ;
                    }
                    else
                    {
                        break ;
                    }
                }
                else
                {
                    daemon_ignore_fit ();
                    // ilog ("%d [%s:%s]\n", chars, program_invocation_short_name , _str);
                }
            }
            else if ( strstr ( buffer, "code=" ) )
            {
                if ( sscanf ( &buffer[0], "code=%d", &__fit_info.code ) == 1 )
                {
                    valid_code = true ;
                }
            }
            else if ( strstr ( buffer, "hits=" ) )
            {
                if ( sscanf ( &buffer[0], "hits=%d", &__fit_info.hits ) == 1 )
                {
#ifdef WANT_HIT_THROTTLE
                    throttle_count =  0 ;

                    if ( __fit_info.hits > 10 )
                        throttle_max = 10 ;
                    else
                        throttle_max = 1  ;
#else
                    ;
#endif
                }
            }
            else if ( strstr ( buffer, "host=" ) )
            {
                char _str1[MAX_CHARS_HOSTNAME+1];
                char _str2[MAX_CHARS_HOSTNAME+1];
                char _str3[MAX_CHARS_HOSTNAME+1];
                char _str4[MAX_CHARS_HOSTNAME+1];
                memset ( _str1, 0 , MAX_CHARS_HOSTNAME+1 );
                memset ( _str2, 0 , MAX_CHARS_HOSTNAME+1 );
                memset ( _str3, 0 , MAX_CHARS_HOSTNAME+1 );
                memset ( _str4, 0 , MAX_CHARS_HOSTNAME+1 );
                int rc = sscanf ( &buffer[0], "host=%32s %32s %32s %32s", &_str1[0], &_str2[0], &_str3[0], &_str4[0] );
                if ( rc )
                {
                    __fit_info.host = _str1 ;
                    if ( rc > 1 )
                    {
                        __fit_info.host.append(" ");
                        __fit_info.host.append(_str2 );
                    }
                    if ( rc > 2 )
                    {
                        __fit_info.host.append(" ");
                        __fit_info.host.append(_str3);
                    }
                    if ( rc > 3 )
                    {
                        __fit_info.host.append(" ");
                        __fit_info.host.append(_str4);
                    }
                }
            }
            else if ( strstr ( buffer, "name=" ) )
            {
                char _str1[60];
                char _str2[60];
                char _str3[60];
                memset ( _str1, 0 , 60 );
                memset ( _str2, 0 , 60 );
                memset ( _str3, 0 , 60 );
                int rc = sscanf ( &buffer[0], "name=%59s %59s %59s", &_str1[0], &_str2[0], &_str3[0] );
                if ( rc )
                {
                    __fit_info.name = _str1 ;
                    if ( rc > 1 )
                    {
                        __fit_info.name.append(" ");
                        __fit_info.name.append(_str2 );
                    }
                    if ( rc > 2 )
                    {
                        __fit_info.name.append(" ");
                        __fit_info.name.append(_str3);
                    }
                }
            }
            else if ( strstr ( buffer, "data=" ) )
            {
                char _str[BUFFER];
                memset ( _str, 0 , BUFFER );
                if ( sscanf ( &buffer[0], "data=%1023s", &_str[0] ) == 1 )
                {
                    __fit_info.data = _str ;
                }
            }
            memset (buffer, 0 , BUFFER );
        } /* end while */
        fclose(_stream);

    }
    if ( !correct_process )
    {
        return (PASS);
    }

    daemon_print_fit ();

    if ( !valid_code )
    {
        elog ( "FIT file parse error (%d)\n", valid_code );
        daemon_init_fit ();
    }
    else
    {
        daemon_log_value   ( "/var/run/fit/fithits", "hits =", __fit_info.hits );
        daemon_remove_file ( "/var/run/fit/fitdone" );
    }
    daemon_rename_file ( FIT__INFO_FILEPATH, FIT__INFO_FILENAME, FIT__INFO_FILENAME_RENAMED );

#endif
    return (PASS);
}

/* Check for fault insertion */
bool daemon_want_fit ( int code )
{
#ifdef WANT_FIT_TESTING
    if ( __fit_info.hits > 0)
    {
        if ( daemon_get_cfg_ptr()->testmode )
        {
            if ( __fit_info.code )
            {
                //ilog ("hits %d code %d:%d\n", __fit_info.hits, __fit_info.code, code);
                if ( __fit_info.code == code )
                {
                    daemon_handle_hit ();
                    return (true) ;
                }
            }
        }
    }
#else
    UNUSED(code);
#endif
   return (false);
}

bool daemon_want_fit ( int code, string host )
{
#ifdef WANT_FIT_TESTING
    if ( __fit_info.hits > 0 )
    {
        if ( daemon_get_cfg_ptr()->testmode )
        {
            if ( __fit_info.code == code )
            {
                //ilog ("%s:%s hits %d code %d:%d\n", host.c_str(), __fit_info.host.c_str() , __fit_info.hits, __fit_info.code, code);
                if (( __fit_info.code == code ) &&
                    ( __fit_info.host.find(host) != std::string::npos ))
                {
                    daemon_handle_hit ();
                    return (true) ;
                }
            }
        }
    }
#else
    UNUSED(code);
    UNUSED(host);
#endif
    return (false);
}

bool daemon_want_fit ( int code, string host, string name )
{
#ifdef WANT_FIT_TESTING
    if ( __fit_info.hits > 0 )
    {
        if ( daemon_get_cfg_ptr()->testmode )
        {
            if ( __fit_info.code == code )
            {
                //ilog ("%s:%s <%s:%s> hits %d code %d:%d\n", host.c_str(), __fit_info.host.c_str(), name.c_str(), __fit_info.name.c_str(), __fit_info.hits, __fit_info.code, code);
                if (( __fit_info.code == code ) &&
                    (( __fit_info.host.find(host) != std::string::npos ) || ( host == "any" )) &&
                     ( __fit_info.name.find(name) != std::string::npos ))
                {
                    daemon_handle_hit ();
                    return (true) ;
                }
            }
        }
    }
#else
    UNUSED(code);
    UNUSED(host);
    UNUSED(name);
#endif
    return (false);
}

bool daemon_want_fit ( int code, string host, string name, string & data )
{
#ifdef WANT_FIT_TESTING
    if ( __fit_info.hits > 0 )
    {
        if ( daemon_get_cfg_ptr()->testmode )
        {
            if ( __fit_info.code == code )
            {
                //ilog ("%s:%s <%s:%s> hits %d code %d:%d\n", host.c_str(), __fit_info.host.c_str(), name.c_str(), __fit_info.name.c_str(), __fit_info.hits, __fit_info.code, code);
                if (( __fit_info.code == code ) &&
                    (( __fit_info.host.find(host) != std::string::npos ) || ( host == "any" )) &&
                     ( __fit_info.name.find(name) != std::string::npos))
                {
                    data = __fit_info.data ;
                    daemon_handle_hit ();
                    return (true) ;
                }
            }
        }
    }
#else
    UNUSED(code);
    UNUSED(host);
    UNUSED(name);
    UNUSED(data);
#endif
    return (false);
}

