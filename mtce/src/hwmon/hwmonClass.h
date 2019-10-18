#ifndef __INCLUDE_HWMONCLASS_H__
#define __INCLUDE_HWMONCLASS_H__

/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

#include "nodeBase.h"      /* for ...                                  */
#include "hostUtil.h"      /* for ... server_enum                      */
#include "httpUtil.h"      /* for ... libEvent                         */
#include "pingUtil.h"      /* for ... ping                             */
#include "threadUtil.h"    /* for ... thread_ctrl_type thread_info_type*/
#include "hwmon.h"
#include "hostClass.h"
#include "hwmonThreads.h"
#include "hwmonSensor.h"
#include "bmcUtil.h"       /* for ... board mgmnt utility header         */

typedef enum
{
    HWMON_DEL__START = 0,
    HWMON_DEL__WAIT,
    HWMON_DEL__DONE,
    HWMON_DEL__STAGES
} hwmon_delStages_enum ;

class hwmonHostClass
{
    private:
    struct hwmon_host {

        string hostname ;

        /** The IP address of the host's board management controller */
        string bm_ip ;

        /** The PW of the host's board management controller */
        string bm_pw  ;

        /** A string label that represents the board management
         *  controller type for this host */
        string bm_type ;

        /** The operator provisioned board management hostname */
        string bm_un ;

        bool bm_provisioned ;

        int empty_secret_log_throttle ;

        libEvent secretEvent ;

        /**  The BMC is 'accessible' once provisioning data is available
         *   and bmc is verified pingable.
         **/
        bool accessible;

        /** run the delete_handler FSM when set to true */
        bool host_delete ;

        /** general purpose retry counter */
        int  retries     ;

        /** true when host is degraded due to the inability to load group/sensor configuration */
        bool degraded ;

        /** true when the SENSORCFG alarm is raised due to the inability to load group/sensor configuration */
        bool alarmed ;

        /** true when sensor config alarm is raised */
        bool alarmed_config ;

        /* sensor audit interval */
        int  interval ;
        int  interval_old ; /* helps show interval change in log */
        bool interval_changed ;

        /* throttle degrade audit logs */
        int degrade_audit_log_throttle ;

        /* throttle log stating its waiting for prococol from mtce */
        int general_log_throttle ;

        /** set to the protocol used to communicate with this server's BMC */
        bmc_protocol_enum protocol ;

        /** Pointer to the previous host in the list */
        struct hwmon_host * prev;

        /** Pointer to the next host in the list */
        struct hwmon_host * next;

        struct mtc_timer hostTimer    ;
        struct mtc_timer addTimer     ;
        struct mtc_timer secretTimer  ;

        bool monitor ; /* true if host's sensors are to be monitored */

        /* set true by HWMON_SENSOR_MONITOR__POWER handling state before the
         * sensor model has been learned. Being false provides hold off
         * to learning the sensor model ; which will be created incorrectly
         * if learned while the power is off */
        bool poweron ;

        /* SENSORS */
        /* ------- */

        /**** New Host Specific Private Constructs for IPMI Monitoring ****/

        bool quanta_server ;

        /* for bmc ping access monitor */
        ping_info_type ping_info ;

        /* Sensor Monitoring Thread Structs */

        /* the info required by the sensor read thread to issue a bmc
         * lanplus request to read sensors over the network */
        thread_ctrl_type bmc_thread_ctrl ; /* control data used to manage the thread */
        thread_info_type bmc_thread_info ; /* thread info used to execute and post results */
        thread_extra_info_type thread_extra_info ; /* extra thread info for sensor monitoring */

        /* Ipmi sensor monitoring control structure */
        monitor_ctrl_type monitor_ctrl ;

        /* number of sensor queries since last process restart */
        int sensor_query_count ;

        int want_degrade_audit ;

        /* the last json string containing the last read sensor data */
        string     json_bmc_sensors ;

        int          sensors ; /**< # of sensors in the sysinv database        */
        int          samples ; /**< # of parsed samples from the reader thread */
        /*
         *  The Main running Sensors Profile for this host.
         *  This list reflects what is in the sysinv database
         *  and shown in the UI.
         */
        sensor_type                    sensor[MAX_HOST_SENSORS] ;

        sensor_data_type   sample[MAX_HOST_SENSORS] ; /* last read analog samples */

        /*
         *  Sequential checksum of all the sensor names in ther various
         *  sensor lists. See hwmonUtil.cpp for checksum utilities or
         *  hwmon.h for prototype
         */
        unsigned short last_sample_sensor_checksum ;
        unsigned short      sample_sensor_checksum ;
        unsigned short     profile_sensor_checksum ;

        /* GROUPS */
        /* ------ */

        /* number of sensors groups provisioned . host */
        int                groups  ;

        /* list of groups for this host */
        struct sensor_group_type group[MAX_HOST_GROUPS] ;

        /* current group monitoring index ; used by the group monitor FSM */
        int group_index ;

        hwmon_addStages_enum    addStage         ;
        hwmon_delStages_enum    delStage         ;

        int group_mon_log_throttle ;

        libEvent event ;

        /* indicates whether the group/sensor accounting looks valid.
         * i.e. number of sensors in sensor groups adds to equal total
         * number of sensors */
        bool accounting_ok ;

        /* The number of sensor accounting errors , i.e. sensors not found,
         * in the current sample set.
         *
         * If this count reaches MAX_SENSORS_NOT_FOUND then the
         * accounting_bad_b4_reload_count below is incremented.
         *
         * Whenever all the sensors are found then
         * this and the accounting_bad_b4_reload_count is cleared. */
        int accounting_bad_count ;

        /* string that represents the BMC firmware version */
        string bmc_fw_version ;

        /********** Sensor Model Relearn Handling Controls **********/

        /* set to true when a new relearn request is received while not
         * already in sensor model relearning mode */
        bool relearn_request ;

        /* true while in sensor model relearning mode */
        bool relearn ;

        /* a timer that forces exit from learn mode when it expires */
        struct mtc_timer relearnTimer ;

        /* Count relearn failure retries.
         * Used to avoid repeating some retry operations. */
        int relearn_retry_counter ;

        /* Store the date/time when learning mode will be disabled.
         * Put into error message to tell the administrator when the
         * next sensor relearn is permitted when the current request
         * is rejected due to already being in relearn mode. */
        string relearn_done_date ;

        /* a structure used to preserved some key sensor model attributes
         * so that they can be restored over/after the relearn action */
        model_attr_type model_attributes_preserved ;
    };

   /** List of allocated host memory.
    *
    * An array of host pointers.
    */
    hwmon_host * host_ptrs[MAX_HOSTS] ;

   /** A memory allocation counter.
    *
    * Should represent the number of hosts in the linked list.
    */
    int memory_allocs ;

    /** A memory used counter
    *
    * A variable storing the accumulated host memory
    */
    int memory_used ;

    struct hwmon_host * hwmon_head ; /**< Host Linked List Head pointer */
    struct hwmon_host * hwmon_tail ; /**< Host Linked List Tail pointer */

    struct hwmonHostClass::hwmon_host* newHost ( void );
    struct hwmonHostClass::hwmon_host* addHost ( string hostname );
    struct hwmonHostClass::hwmon_host* getHost ( string hostname );
    int                                remHost ( string hostname );
    int                                delHost ( struct hwmonHostClass::hwmon_host * hwmon_host_ptr );
    struct hwmonHostClass::hwmon_host* getHost_timer ( timer_t tid );

    int  set_bm_prov         ( struct hwmonHostClass::hwmon_host * host_ptr, bool state );
    void clear_bm_assertions ( struct hwmonHostClass::hwmon_host * host_ptr );
    void free_host_timers    ( struct hwmonHostClass::hwmon_host * host_ptr );

    /** typically called by an audit, this interface cycles through all
     * the sensors looking for any that are in the degrade state and
     * sends a degrade request to maintenance if it sees just 1 */
    void  degrade_state_audit ( struct hwmonHostClass::hwmon_host * host_ptr );

    /* FSM handlers */
    int add_host_handler  ( struct hwmonHostClass::hwmon_host * host_ptr );
    int group_mon_handler ( struct hwmonHostClass::hwmon_host * host_ptr );

    /* in hwmonSensor.cpp */
    int hwmon_load_sensors    ( struct hwmonHostClass::hwmon_host * host_ptr , bool & error );
    int hwmon_load_groups     ( struct hwmonHostClass::hwmon_host * host_ptr , bool & error );

    int load_profile_sensors  ( struct hwmonHostClass::hwmon_host * host_ptr,
                                sensor_type * sensor_array_ptr, int max,
                                bool & error );

    int load_profile_groups   ( struct hwmonHostClass::hwmon_host * host_ptr,
                                struct sensor_group_type * group_array_ptr,int max ,
                                bool & error );


    int hwmon_group_sensors   ( struct hwmonHostClass::hwmon_host * host_ptr );

    int delete_unwanted_sensors ( struct hwmonHostClass::hwmon_host * host_ptr );

    /** Host add handler Stage Change member function */
    int addStageChange         ( struct hwmonHostClass::hwmon_host * hwmon_host_ptr,
                                 hwmon_addStages_enum newHdlrStage );

    /** handled deleting a host from the hwmonHostClass object */
    int delete_handler   ( struct hwmonHostClass::hwmon_host * host_ptr );

    void mem_log_info    ( struct hwmonHostClass::hwmon_host * host_ptr );
    void mem_log_options ( struct hwmonHostClass::hwmon_host * host_ptr );
    void mem_log_bm      ( struct hwmonHostClass::hwmon_host * host_ptr );
    void mem_log_groups  ( struct hwmonHostClass::hwmon_host * host_ptr );
    void mem_log_threads ( struct hwmonHostClass::hwmon_host * host_ptr );

    /************* New Private APIs for IPMI Sensor Monitoring  **************/

    /*************************************************************************
     *
     * Implemented in hwmonClass.cpp
     *
     *************************************************************************/

    void bmc_data_init ( struct hwmonHostClass::hwmon_host * host_ptr );

    /***************************************************************************
     *
     * The following are sensor model provisioning APIs responsible for
     * loading, creating and deleting sensor models wrt the sysinv database
     * and hwmond.
     *
     * Implemented in hwmonModel.cpp
     *
     * bmc_load_sensor_model   - will load an existing sensor and group
     *                            model from the database  for the specified
     *                            host into hwmond.
     *
     * bmc_create_sensor_model - will create a new sensor and group model in
     *                            the sysinv database for the specified host.
     *
     * bmc_delete_sensor_model - will delete the sensor and group model from
     *                            the sysinv database for the specified host.
     *
     * bmc_create_sample_model - will create a sensor model based on sample
     *                            data for the specified host.
     *
     * bmc_create_quanta_model - will create a quanta server sensor group model
     *                            for the specified host from sensor sample data.
     *
     *************************************************************************/
    int  bmc_load_sensor_model   ( struct hwmonHostClass::hwmon_host * host_ptr );
    int  bmc_create_sensor_model ( struct hwmonHostClass::hwmon_host * host_ptr );
    int  bmc_delete_sensor_model ( struct hwmonHostClass::hwmon_host * host_ptr );
    int  bmc_create_sample_model ( struct hwmonHostClass::hwmon_host * host_ptr );
    int  bmc_create_quanta_model ( struct hwmonHostClass::hwmon_host * host_ptr );

    /*************************************************************************
     *
     * The following are sensor sample sensor data management APIs
     *
     * File: hwmonBmc.cpp
     *
     * bmc_load_sensor_samples   - loads the samples into the sample list.
     *
     * bmc_update_sensors        - updates the hwmond with the latest sensor
     *                              sample severity level for the specified host.
     *
     *************************************************************************/
    int  bmc_load_sensor_samples ( struct hwmonHostClass::hwmon_host * host_ptr, char * msg_ptr );
    int  bmc_update_sensors      ( struct hwmonHostClass::hwmon_host * host_ptr );

    /**************************************************************************
     *
     * Name   : manage_startup_states
     *
     * Purpose: Manage how hwmon deals with sensor states over process startup.
     *
     * File   : hwmonHdlr.cpp
     *
     *   This code that was taken from the add_handler and put into this stand
     *   alone procedure for code re-use so that it can be called by the add
     *   handler for bmc without cloning it.
     *
     **************************************************************************/
    bool manage_startup_states ( struct hwmonHostClass::hwmon_host * host_ptr );

    /**************************************************************************
     *
     *   Handle bmc monitoring audit interval changes where there is one
     *   interval for all sensor groups. Changing a single group's audit
     *   interval does so for all. All for 1 and one for all.
     *
     **************************************************************************/
    int  interval_change_handler( struct hwmonHostClass::hwmon_host * host_ptr );

    /*   The sensor monitor FSM */
    int  bmc_sensor_monitor ( struct hwmonHostClass::hwmon_host * host_ptr );

    /*   Remove all groups / sensor from hwmon */
    int  hwmon_del_groups    ( struct hwmonHostClass::hwmon_host * host_ptr );
    int  hwmon_del_sensors   ( struct hwmonHostClass::hwmon_host * host_ptr );

    /* Implemented in hwmonGroup.cpp */

    /***************************************************************************
     *   Manage sensor group states in the database and hwmon as well
     *   and manage sensr group alarms. Since state changes affect alarming
     *   the two functions work well together.
     ***************************************************************************/
    int  bmc_set_group_state      ( struct hwmonHostClass::hwmon_host * host_ptr, string state );

    /*   Set all sensors to disabled-offline state/status */
    int  bmc_disable_sensors  ( struct hwmonHostClass::hwmon_host * host_ptr );

    /****************************************************************************
     *   Create sensor groups in hwmon based on sample data using similar bmc
     *   unit type canned groups and save those groups into the database.
     ****************************************************************************/
    int  bmc_create_groups ( struct hwmonHostClass::hwmon_host * host_ptr );

    /****************************************************************************
     *   Load the sensor samples into hwmon and then save them into the database.
     ****************************************************************************/
    int  bmc_create_sensors ( struct hwmonHostClass::hwmon_host * host_ptr );

    /*****************************************************************************
     * Add a new group to hwmon and then to the sysinv database.
     ****************************************************************************/
    int  bmc_add_group ( struct hwmonHostClass::hwmon_host * host_ptr ,
                          string datatype, string sensortype,
                          canned_group_enum grouptype,
                          string group_name, string path );

    /****************************************************************************
     *   Put the current bmc sensor list into the previously created sensor type
     *   based groups and save that grouping in the sysinv database.
     *****************************************************************************/
    int  bmc_group_sensors ( struct hwmonHostClass::hwmon_host * host_ptr );

    /***************************************************************************
     *   Check whether the group/sensor accounting looks valid.
     *   i.e. number of sensors in sensor groups adds to equal total sensors.
     **************************************************************************/
    void check_accounting    ( struct hwmonHostClass::hwmon_host * host_ptr );

    /***************************************************************************
     *   Force monitoring to start now
     **************************************************************************/
    void monitor_now ( struct hwmonHostClass::hwmon_host * host_ptr );

    /***************************************************************************
     *   Force monitoring to start soon ; called during sensor relearn request
     *   to give horizon time to show the deleted sensor model but not have
     *   the user wait for what might be a long audit interval before the
     *   refresh.
     **************************************************************************/
    void monitor_soon ( struct hwmonHostClass::hwmon_host * host_ptr );

    /**************************************************************************
     *   Save and restore structure and utilties for preserving audit
     *   interval and group actions over a sensor relearn.
     **************************************************************************/

    void    save_model_attributes ( struct hwmonHostClass::hwmon_host * host_ptr );
    void restore_group_actions    ( struct hwmonHostClass::hwmon_host * host_ptr,
                                             struct sensor_group_type * group_ptr );

    /*************************************************************************/

    void sensorState_print_debug ( struct hwmonHostClass::hwmon_host * host_ptr, string sensorname, string proc, int line );

    public:

     hwmonHostClass(); /**< constructor */
    ~hwmonHostClass(); /**< destructor  */

    hostBaseClass hostBase ;

    system_type_enum system_type ;

    void timer_handler ( int sig, siginfo_t *si, void *uc);

    /** This is a list of host names. */
    std::list<string>           hostlist ;
    std::list<string>::iterator hostlist_iter_ptr ;

    void hwmon_fsm ( void );

    bool is_bm_provisioned ( string hostname );

    string get_bm_ip    ( string hostname );
    string get_bm_type  ( string hostname );
    string get_bm_un    ( string hostname );
    string get_hostname ( string uuid ); /**< lookup hostname from the host uuid */

    string get_relearn_done_date ( string hostname );

    int hosts ;

    /* This bool is set in the daemon_configure case to inform the
     * FSM that there has been a configuration reload.
     * The initial purpose if this bool is to trigger a full sensor
     * dump of all hosts on demand */
    bool config_reload ;

    /*********  New Public Constructs for IPMI Sensor Monitoring  ***********/

    /* set to true once a host has been deleted. This will cause the FSM to
     * kick out of the host list to be restarted without this host in it
     * any more */
    bool host_deleted ;

    /* sets the want_degrade_audit = true for all hosts */
    void set_degrade_audit ( void );

    /************************************************************************/
    int  add_host ( node_inv_type & inv );
    int  mod_host ( node_inv_type & inv );
    int  del_host ( string hostname );
    int  rem_host ( string hostname );
    int  mon_host ( string hostname, bool monitor );
    int  request_del_host ( string hostname );

    int  bmc_learn_sensor_model ( string uuid );

    /****************************************************************************
     *
     * Name:        get_sensor
     *
     * Description: Returns a pointer to the host sensor
     *              that matches the supplied sensor name.
     *
     ****************************************************************************/
    sensor_type * get_sensor ( string hostname, string sensorname );

    /****************************************************************************
     *
     * Name:        add_sensor
     *
     * Description: If the return code is PASS then the supplied sensor is
     *              provisioned against this host. If the sensor already exists
     *              then it is updated with all the new information. Otherwise
     *              (normally) a new sensor is added.
     *
     ****************************************************************************/
    int  add_sensor ( string hostname, sensor_type       & sensor );

    /****************************************************************************
     *
     * Name:        add_sensor_uuid
     *
     * Description: Adds the sysinv supplied sensor uuid to hwmon for
     *              the specified sensor/host.
     *
     ****************************************************************************/
    int  add_sensor_uuid ( string & hostname, string & name, string & uuid );

    /****************************************************************************
     *
     * Name:        hwmon_get_group
     *
     * Description: Returns a pointer to the host sensor group
     *              that matches the supplied sensor group name.
     ****************************************************************************/
    struct sensor_group_type * hwmon_get_group ( string hostname, string group_name );

    /****************************************************************************
     *
     * Name:        hwmon_get_sensorgroup
     *
     * Description: Returns a pointer to the host sensor group
     *              that matches the supplied sensor name.
     ****************************************************************************/
    struct sensor_group_type * hwmon_get_sensorgroup ( string hostname, string sensorname );

    /****************************************************************************
     *
     * Name:        hwmon_add_group
     *
     * Description: If the return code is PASS then the supplied sensor group is
     *              provisioned against this host. If the group already exists
     *              then it is updated with all the new information. Otherwise
     *              (normally) a new group is added to the hwmon class struct.
     *
     ****************************************************************************/
    int  hwmon_add_group  ( string hostname, struct sensor_group_type & sensor_group );

    /****************************************************************************
     *
     * Name:        add_group_uuid
     *
     * Description: Adds the sysinv supplied group uuid to hwmon for
     *              the specified group/host.
     *
     ****************************************************************************/
    int  add_group_uuid ( string & hostname, string & name, string & uuid );

    int  group_modify ( string hostname, string group, string field, string value );

    /* TODO: make this a struct hwmonHostClass::hwmon_host * host_ptr */
    int manage_sensor_state ( string & hostname, sensor_type * sensor, sensor_severity_enum severity );

    void memLogDelimit    ( void );              /**< Debug log delimiter    */
    void memDumpNodeState ( string hostname );
    void memDumpAllState  ( void );
    void print_node_info  ( void );              /**< Print node info banner */

    /************  New Public API for IPMI Sensor Monitoring    *************/

    /* Sets a flag that indicates the sensor audit interval has changed.
     *
     * The DELAY phase of sensor monitoring will look at and will handle
     * the change as a background operation. */
    void audit_interval_change ( string hostname );

    /* Sets host_ptr->interval to the specified value and sets a flag
     * that indicates the sensor audit interval has changed.
     *
     * The DELAY phase of sensor monitoring will look at thes flag this
     * API sets and will handle the change as a background operation. */
    void modify_audit_interval ( string hostname , int interval );

    /************************************************************************/
};

hwmonHostClass * get_hwmonHostClass_ptr ( void );

#endif /* __INCLUDE_HWMONCLASS_H__ */
