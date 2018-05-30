/*
 * Copyright (c) 2015-2016 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

 /**
  * @file
  * Wind River CGCS Platform Resource Monitor Fault Management
  * retry mechanism for fault set/clear failures due to communication
  * error.
  */

#include <pthread.h>
#include "rmon.h"


typedef enum
{
    CLR_REQUEST = 0,
    SET_REQUEST = 1

} fm_req_type_t;


typedef struct fm_req_info
{
    fm_req_type_t req_type;
    union
    {
        AlarmFilter alarm_filter;
        SFmAlarmDataT alarm;

    } _data;
    struct fm_req_info *next;

} fm_req_info_t;


typedef struct fm_req_queue
{
    fm_req_info_t *head;
    fm_req_info_t *tail;
    pthread_mutex_t mutex;

} fm_req_queue_t;


static fm_req_queue_t fm_req_q;
static void rmon_fm_enq(fm_req_type_t req, void *data);
static void rmon_fm_deq(void);


/****************************/
/* Initialization Utilities */
/****************************/
void rmon_fm_init (void)
{
    fm_req_q.head = NULL;
    fm_req_q.tail = NULL;
    pthread_mutex_init(&fm_req_q.mutex, NULL);
}

void rmon_fm_fini (void)
{
    pthread_mutex_lock(&fm_req_q.mutex);
    fm_req_info_t *i = fm_req_q.head;
    while (i != NULL) {
        fm_req_info_t *n = i->next;
        delete i;
        i = n;
    }
    fm_req_q.head = fm_req_q.tail = NULL;
    pthread_mutex_unlock(&fm_req_q.mutex);
    pthread_mutex_destroy(&fm_req_q.mutex);
}

/***************************/
/* handler function        */
/***************************/
void rmon_fm_handler (void)
{
    while (fm_req_q.head != NULL) {
        EFmErrorT err;
        fm_req_info_t *fm_req = fm_req_q.head;
        if (fm_req->req_type == CLR_REQUEST) {
            ilog("clearing alarm %s", fm_req->_data.alarm_filter.entity_instance_id);
            err = fm_clear_fault (&fm_req->_data.alarm_filter);
        }
        else {
            ilog("setting alarm %s", fm_req->_data.alarm.entity_instance_id);
            err = fm_set_fault (&fm_req->_data.alarm, NULL);
        }

        if (err == FM_ERR_NOCONNECT) {
            ilog("FM_ERR_NOCONNECT");
            return;
        }

        rmon_fm_deq();
    }
}

/*****************************/
/* request functions         */
/*****************************/
EFmErrorT rmon_fm_clear (AlarmFilter *alarmFilter)
{
    EFmErrorT err = FM_ERR_NOCONNECT;
    if (fm_req_q.head == NULL)
        err = fm_clear_fault (alarmFilter);
    if (err == FM_ERR_NOCONNECT) {
        ilog("retry clearing alarm %s", alarmFilter->entity_instance_id);
        rmon_fm_enq (CLR_REQUEST, (void *) alarmFilter);
        return FM_ERR_OK;
    }

    return err;
}

EFmErrorT rmon_fm_set (const SFmAlarmDataT *alarm, fm_uuid_t *fm_uuid)
{
    EFmErrorT err = FM_ERR_NOCONNECT;
    if (fm_req_q.head == NULL)
        err = fm_set_fault (alarm, fm_uuid);
    if (err == FM_ERR_NOCONNECT) {
        ilog("retry setting alarm %s", alarm->entity_instance_id);
        rmon_fm_enq (SET_REQUEST, (void *) alarm);
        return FM_ERR_OK;
    }

    return err;
}

EFmErrorT rmon_fm_get (AlarmFilter *alarmFilter, SFmAlarmDataT **alarm, unsigned int *num_alarm)
{
    unsigned int n = 0;
    EFmErrorT err = FM_ERR_NOT_ENOUGH_SPACE;

    while (err == FM_ERR_NOT_ENOUGH_SPACE) {
        /* get additional 3 more alarms at a time, as max. number of port alarms
           is 6 (2 ports per interface: OAM, INFRA, MGMT */
        n += 3;
        SFmAlarmDataT *list = (SFmAlarmDataT *) malloc(sizeof(SFmAlarmDataT) * n);
        if (list != NULL) {
            err = fm_get_faults (&alarmFilter->entity_instance_id, list, &n);
            if (err == FM_ERR_OK) {
                *alarm = list;
                *num_alarm = n;
                return FM_ERR_OK;
            }
            free(list);
        }
        else {
            err = FM_ERR_NOMEM;
        }
   }

   *alarm = NULL;
   *num_alarm = 0;

   return err;
}


/****************************/
/* queue functions          */
/****************************/
void rmon_fm_deq (void)
{
    pthread_mutex_lock (&fm_req_q.mutex);
    fm_req_info_t *fm_req = fm_req_q.head;
    if (fm_req->next == NULL) {
        fm_req_q.head = fm_req_q.tail = NULL;
    }
    else {
        fm_req_q.head = fm_req->next;
    }
    pthread_mutex_unlock (&fm_req_q.mutex);
    delete fm_req;
}

void rmon_fm_enq (fm_req_type_t req, void *data)
{
    fm_req_info_t *fm_req = new fm_req_info_t;
    fm_req->next = NULL;
    fm_req->req_type = req;
    if (req == CLR_REQUEST)
        fm_req->_data.alarm_filter = *((AlarmFilter *)data);
    else
        fm_req->_data.alarm = *((SFmAlarmDataT *)data);
    pthread_mutex_lock (&fm_req_q.mutex);
    if (fm_req_q.tail == NULL) {
        fm_req_q.head = fm_req_q.tail = fm_req;
    }
    else {
        fm_req_q.tail->next = fm_req;
        fm_req_q.tail = fm_req;
    }
    pthread_mutex_unlock (&fm_req_q.mutex);
}

