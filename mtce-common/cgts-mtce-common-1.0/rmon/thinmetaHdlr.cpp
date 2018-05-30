/*
 * Copyright (c) 2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/**
 * @file
 * Wind River Titanium Cloud Platform, LVM Thinpool Metadata Monitor Handler
 */
#include "rmon.h"        /* rmon header file */

/* Used to set interface alarms through the FM API */
static SFmAlarmDataT alarmData;

/*******************************************************************************
 *
 * Name    : _build_entity_instance_id
 *
 * Purpose : Build the entity instance id needed by our alarm
 *
 * *****************************************************************************/
void thinmeta_init(thinmeta_resource_config_type * res, struct mtc_timer * timers, int count) {
    if (count > MAX_RESOURCES) {
        elog("Thinpool metadata resource 'count' is: %i, maximum number or resources is: %i, "
             "initializing count to max!",
              count, MAX_RESOURCES);
        count = MAX_RESOURCES;
    }

    for (int i = 0; i < count; i++) {

        /* Mark first execution after reloading the configuration */
        res[i].first_run = true;

        /* Init timer defaults for this resource */
        mtcTimer_init ( timers[i] ) ;
        timers[i].hostname = "localhost" ;
        timers[i].service  = res[i].thinpool_name ;
        timers[i].ring = true;  // set it to true for the initial run

    }
}

/*******************************************************************************
 *
 * Name    : _build_entity_instance_id
 *
 * Purpose : Build the entity instance id needed by our alarm
 *
 * *****************************************************************************/
void _build_entity_instance_id(thinmeta_resource_config_type * ptr, char * entity) {
    if (!entity) {
        elog("%s/%s pool alarm failed to create entity instance id, 'entity' is NULL!",
             ptr->vg_name, ptr->thinpool_name);
        return;
    }
    rmon_ctrl_type * _rmon_ctrl_ptr;
    _rmon_ctrl_ptr = get_rmon_ctrl_ptr();

    snprintf(entity, sizeof(alarmData.entity_instance_id),
            "%s.lvmthinpool=%s/%s", _rmon_ctrl_ptr->my_hostname, ptr->vg_name, ptr->thinpool_name);
}

/*******************************************************************************
 *
 * Name    : _set_thinmeta_alarm
 *
 * Purpose : Set or clears the threshold alarm
 *
 * *****************************************************************************/
void _set_thinmeta_alarm( thinmeta_resource_config_type * ptr)
{
    strcpy(alarmData.uuid, "");
    strcpy(alarmData.entity_type_id ,"system.host");
    _build_entity_instance_id(ptr, alarmData.entity_instance_id);
    alarmData.alarm_state =  FM_ALARM_STATE_SET;
    alarmData.alarm_type =  FM_ALARM_OPERATIONAL;
    alarmData.probable_cause =  FM_ALARM_STORAGE_PROBLEM;
    if ( ptr->autoextend_on ) {
        snprintf(alarmData.reason_text , sizeof(alarmData.reason_text),
                "Metadata usage for LVM thin pool %s/%s "
                "exceeded threshold and automatic extension failed; "
                "threshold: %u%%, actual: %.2f%%.",
                ptr->vg_name, ptr->thinpool_name,
                ptr->critical_threshold, ptr->resource_value);
        snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action),
                "Increase Storage Space Allotment for Cinder on the 'lvm' backend. "
                "Consult System Administration Manual for more details. "
                "If problem persists, contact next level of support.");
    }
    else {
        snprintf(alarmData.reason_text , sizeof(alarmData.reason_text),
                "Metadata usage for LVM thin pool %s/%s exceeded threshold; "
                "threshold: %u%%, actual: %.2f%%.",
                ptr->vg_name, ptr->thinpool_name, ptr->critical_threshold, ptr->resource_value);
        snprintf(alarmData.proposed_repair_action , sizeof(alarmData.proposed_repair_action),
                "Extend the metadata LV with 'lvextend --poolmetadatasize "
                "+<size_to_extend_in_MiB>M %s/%s'. "
                "Consult System Administration Manual for more details. "
                "If problem persists, contact next level of support.",
                ptr->vg_name, ptr->thinpool_name);
    }
    alarmData.timestamp = 0;
    alarmData.service_affecting = FM_FALSE;
    alarmData.suppression = FM_TRUE;
    alarmData.severity = FM_ALARM_SEVERITY_CRITICAL;
    strcpy(alarmData.alarm_id, THINMETA_ALARM_ID);

    dlog("%s/%s pool exceeding usage threshold, raising alarm\n", ptr->vg_name, ptr->thinpool_name);
    int ret = rmon_fm_set(&alarmData, NULL) == FM_ERR_OK;
    if (ret == FM_ERR_OK || ret == FM_ERR_ALARM_EXISTS) {
        if (!ptr->alarm_raised) {
            // log only once to avoid filling logs
            ilog("%s/%s pool exceeding usage threshold, alarm raised", ptr->vg_name, ptr->thinpool_name);
            ptr->alarm_raised = true;
        }
    }
    else {
        elog("Creation of alarm %s for entity instance id: %s failed. Error: %d \n",
                alarmData.alarm_id, alarmData.entity_instance_id, ret);
        ptr->alarm_raised = false;
    }
}

/*****************************************************************************
 *
 * Name    : _clear_thinmeta_alarm
 *
 * Purpose : Clear the alarm of the resource passed in
 *
 *****************************************************************************/
void _clear_thinmeta_alarm ( thinmeta_resource_config_type * ptr )
{
    dlog ("%s/%s below threshold, clearing alarm\n", ptr->vg_name, ptr->thinpool_name);
    AlarmFilter alarmFilter;

    _build_entity_instance_id (ptr, alarmData.entity_instance_id);

    snprintf(alarmFilter.alarm_id, FM_MAX_BUFFER_LENGTH, THINMETA_ALARM_ID);
    snprintf(alarmFilter.entity_instance_id, FM_MAX_BUFFER_LENGTH, alarmData.entity_instance_id);

    int ret = rmon_fm_clear(&alarmFilter);
    if (ret == FM_ERR_OK) {
       ilog ("Cleared stale alarm %s for entity instance id: %s",
             alarmFilter.alarm_id, alarmFilter.entity_instance_id);
       ptr->alarm_raised = false;
    }
    else if (ret == FM_ERR_ENTITY_NOT_FOUND) {
       if (!ptr->first_run) {
           wlog ("Alarm %s for entity instance id: %s was not found",
                 alarmFilter.alarm_id, alarmFilter.entity_instance_id);
       }
       ptr->alarm_raised = false;
    }
    else {
       elog ("Failed to clear stale alarm %s for entity instance id: %s error: %d",
             alarmFilter.alarm_id, alarmFilter.entity_instance_id, ret);
       ptr->alarm_raised = true;
    }
}

/*****************************************************************************
 *
 * Name    : is_pool_ready
 *
 * Purpose : Check if an LVM Thin Pool is configured
 * Return  : PASS/FAIL
 *
 *****************************************************************************/
bool is_pool_ready(thinmeta_resource_config_type * ptr) {
    char result[BUFFER_SIZE];
    int rc = PASS;
    char cmd[BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "timeout 2 lvs --noheadings -o vg_name,lv_name --separator / %s/%s",
             ptr->vg_name, ptr->thinpool_name);
    rc = execute_pipe_cmd(cmd, result, sizeof(result));
    if (rc == 5 || rc == 1) { // ECMD_FAILED or ECMD_PROCESSED
        // pool or VG was not found or not ready
        return false;
    }
    else if (rc) {
        // unexpected error
        elog("%s/%s pool config query failed", ptr->vg_name, ptr->thinpool_name);
        wlog("...cmd: '%s' exit status: %i result: '%s'", cmd, rc, result);
        return false;
    }
    return true;
}

/*****************************************************************************
 *
 * Name    : calculate_metadata_usage
 *
 * Purpose : Obtain the percentage of used metadata space for a thin pool
 *           in thin provisioning.
 * Return  : PASS/FAIL
 *
 *****************************************************************************/
int calculate_metadata_usage(thinmeta_resource_config_type * ptr) {
    char result[BUFFER_SIZE];
    int rc = PASS;
    char meta_usage_cmd[BUFFER_SIZE];

    snprintf(meta_usage_cmd, sizeof(meta_usage_cmd),
             "set -o pipefail; timeout 2 lvs -o metadata_percent --noheadings %s/%s | tr -d ' '",
             ptr->vg_name, ptr->thinpool_name);
    rc = execute_pipe_cmd(meta_usage_cmd, result, sizeof(result));
    if (rc == 1) { // ECMD_PROCESSED
        // sometimes lvs command fail to process, not critical just retry in this case
        dlog("%s/%s pool metadata usage query failed\n", ptr->vg_name, ptr->thinpool_name);
        dlog("...cmd: '%s' exit status: %i result: '%s'\n", meta_usage_cmd, rc, result);
        rc = execute_pipe_cmd(meta_usage_cmd, result, sizeof(result));
    }
    if (rc != PASS) {
        elog("%s/%s pool metadata usage query failed", ptr->vg_name, ptr->thinpool_name);
        wlog("...cmd: '%s' exit status: %i result: '%s'", meta_usage_cmd, rc, result);
        return (FAIL);
    }
    ptr->resource_value = atof(result);
    if ( log_value ( ptr->resource_value,
                     ptr->resource_prev,
                     DEFAULT_LOG_VALUE_STEP ) )
    {
        plog("%s/%s pool metadata usage is: %.2f%%\n",
                ptr->vg_name, ptr->thinpool_name, ptr->resource_value);
    }
    return rc;
}

/*****************************************************************************
 *
 * Name    : extend_thinpool_metadata
 *
 * Purpose : Extend the Logical Volume used by LVM Thin Pool metadata
 * Return  : PASS/FAIL
 *
 *****************************************************************************/
int extend_thinpool_metadata(thinmeta_resource_config_type * ptr) {
    char result[THINMETA_RESULT_BUFFER_SIZE];
    int rc = PASS;
    char cmd[BUFFER_SIZE];

    dlog(">>> ptr->autoextend_percent: %i", ptr->autoextend_percent);
    dlog("%s/%s pool, extending metadata by %i%s\n", ptr->vg_name, ptr->thinpool_name,
         ptr->autoextend_by, ptr->autoextend_percent? "%": "MiB");
    if (ptr->autoextend_percent) {
        char meta_lv_name[BUFFER_SIZE];
        /* Get metadata LV name
         * 'lvextend --poolmetadatasize'  parameter is only allowed in MiB not percents.
         * For percent we need to rely on 'lvextend -l...%LV', but we first have to get
         * the real name of the metadata LV */
        snprintf(cmd, sizeof(cmd),
                 "set -o pipefail; timeout 2 lvs %s/%s -o metadata_lv --noheadings | "
                 "tr -d '[] '",
                 ptr->vg_name, ptr->thinpool_name);
        rc = execute_pipe_cmd(cmd, meta_lv_name, sizeof(meta_lv_name));
        if (rc != PASS) {
            elog("%s/%s pool metadata name query failed. Aborting auto extend.",
                  ptr->vg_name, ptr->thinpool_name);
            return (FAIL);
        }
        dlog("%s/%s pool metadata LV name is: %s\n",
              ptr->vg_name, ptr->thinpool_name, meta_lv_name);
        /* Extend metadata cmd*/
        snprintf(cmd, sizeof(cmd),
                 "timeout 10 lvextend -l +%u%%LV %s/%s",
                 ptr->autoextend_by, ptr->vg_name, meta_lv_name);
    }
    else {
        /* Extend metadata cmd*/
        snprintf(cmd, sizeof(cmd),
                 "timeout 10 lvextend --poolmetadatasize +%uM %s/%s",
                 ptr->autoextend_by, ptr->vg_name, ptr->thinpool_name);
    }
    rc = execute_pipe_cmd(cmd, result, sizeof(result));
    if (rc != PASS) {
        dlog("%s/%s pool metadata size extension failed\n", ptr->vg_name, ptr->thinpool_name);
        dlog("...cmd: '%s' exit status: %i result: '%s'\n", cmd, rc, result);
        return (FAIL);
    }
    return rc;
}

/*****************************************************************************
 *
 * Name    : thinmeta_handler
 *
 * Purpose : Handle the metadata usage and raise alarms through the FM API
 *
 *****************************************************************************/
int thinmeta_handler( thinmeta_resource_config_type * ptr ) {
    if (!ptr) {
        elog ("Function called with NULL pointer!");
        return (PASS);
    }
    switch ( ptr->stage ) {
        case RMON_STAGE__INIT:
            {
                /* Check if pool is ready */
                dlog("%s/%s pool config query", ptr->vg_name, ptr->thinpool_name);
                if (!is_pool_ready(ptr)) {
                    ilog("%s/%s pool not ready, monitoring will be resumed when ready",
                         ptr->vg_name, ptr->thinpool_name);
                    ptr->stage = RMON_STAGE__MONITOR_WAIT;
                }
                else {
                    dlog("%s/%s pool ready",  ptr->vg_name, ptr->thinpool_name);
                    ptr->stage = RMON_STAGE__MONITOR;
                    return (RETRY); // execute next stage immediately
                }
                break;
            }
        case RMON_STAGE__MONITOR_WAIT:
            {
                /* Waiting for pool to be ready*/
                if (is_pool_ready(ptr)) {
                    ilog("%s/%s pool ready, starting monitoring",
                         ptr->vg_name, ptr->thinpool_name);
                    ptr->stage = RMON_STAGE__MONITOR;
                    return (RETRY); // execute next stage immediately
                }
                break;
            }
        case RMON_STAGE__MONITOR:
            {
                dlog("%s/%s pool metadata usage monitoring", ptr->vg_name, ptr->thinpool_name);
                /* calculate usage. The first time we calculate thinpool meta
                 * usage is to get the baseline resource value, if it exceeds
                 * the critical threshold and if the resource configuration
                 * allows us to autoextend thinpools then we do an extend
                 * operation and then check again if our thinpool usage has
                 * fallen below the critical watermark. */
                if(calculate_metadata_usage(ptr) == FAIL) {
                    ptr->stage = RMON_STAGE__INIT;
                    return (RETRY); // execute next stage immediately
                    break;
                }

                /* act on thresholds */
                if((ptr->alarm_raised || ptr->first_run) &&
                        ptr->resource_value < ptr->critical_threshold) {
                    // clear alarm
                    _clear_thinmeta_alarm(ptr);
                }
                else if(ptr->resource_value >= ptr->critical_threshold) {
                    if (ptr->autoextend_on) {
                        // Extend metadata
                        // Retry at each pass (failures are fast) till successful, in case
                        // our VG is extended on the fly and we suddenly get enough space.
                        // Log operation and error only once to avoid filling log file.
                        if(!ptr->alarm_raised) {
                            ilog("%s/%s pool metadata will be extended by: %i%s",
                                 ptr->vg_name, ptr->thinpool_name,
                                 ptr->autoextend_by, ptr->autoextend_percent? "%": "MiB");
                        }
                        if(extend_thinpool_metadata(ptr) == PASS) {
                            // after extension recalculate metadata usage
                            if(calculate_metadata_usage(ptr) == FAIL) {
                                // this was successful < 1s ago, should not happen!
                                elog("%s/%s pool second metadata usage calculation failed!",
                                     ptr->vg_name, ptr->thinpool_name);
                            }
                        }
                        else {
                            if(!ptr->alarm_raised) {
                                elog("%s/%s pool metadata extension failed ",
                                     ptr->vg_name, ptr->thinpool_name);
                            }
                        }
                    }
                    if ((ptr->resource_value >= ptr->critical_threshold) && // resource_value may change
                            ptr->alarm_on) {
                        // raise alarm (if autoextend is disabled or failed)
                        _set_thinmeta_alarm(ptr);
                    }
                    else if (ptr->alarm_on && (ptr->alarm_raised || ptr->first_run)) {
                        // condition also needed if alarm existed prior to rmon startup
                        _clear_thinmeta_alarm(ptr);
                    }
                }
                /* Mark first run as complete */
                ptr->first_run = false;
                break;
            }
        default:
            {
                slog ("%s/%s Invalid stage (%d)\n", ptr->vg_name, ptr->thinpool_name, ptr->stage);
                /* Default to init for invalid case */
                ptr->stage = RMON_STAGE__INIT;
                return (RETRY); // execute next stage immediately
            }
    }
    return (PASS);
}
