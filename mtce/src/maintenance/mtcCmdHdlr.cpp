/*
 * Copyright (c) 2013-2017 Wind River Systems, Inc.
*
* SPDX-License-Identifier: Apache-2.0
*
 */

/****************************************************************************
 * @file
 * Wind River Titanium Cloud Maintenance Command Handler FSM Implementation
 *
 *  nodeLinkClass::cmd_handler
 *
 ****************************************************************************/

using namespace std;

#define __AREA__ "cmd"

#include "nodeClass.h"     /* for ... nodeLinkClass               */
#include "nodeUtil.h"      /* for ... clean_bm_response_files     */
#include "nodeTimers.h"    /* for ... mtcTimer_start/stop         */
#include "mtcNodeMsg.h"    /* for ... send_mtc_cmd                */
#include "nodeCmds.h"      /* for ... Cmd hdl'ing stages & struct */

extern void mtcTimer_handler ( int sig, siginfo_t *si, void *uc);

string _get_cmd_str( int this_cmd )
{
    string temp ;
    switch (this_cmd)
    {
        case MTC_OPER__MODIFY_HOSTNAME:
        {
            temp = "Modify Hostname";
            break ;
        }
        case MTC_OPER__RESET_PROGRESSION:
        {
            temp = "Reset Progression";
            break ;
        }
        case MTC_OPER__HOST_SERVICES_CMD:
        {
            temp = "Host Services";
            break ;
        }
        case MTC_OPER__RUN_IPMI_COMMAND:
        {
            temp = "IPMI Command";
            break ;
        }
        default:
        {
            temp = "_unknown_" ;
        }
    }
    return(temp);
}

void nodeLinkClass::mtcCmd_workQ_dump ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->mtcCmd_work_fifo.size() != 0 )
    {
        for ( node_ptr->mtcCmd_work_fifo_ptr  = node_ptr->mtcCmd_work_fifo.begin() ;
              node_ptr->mtcCmd_work_fifo_ptr != node_ptr->mtcCmd_work_fifo.end();
              node_ptr->mtcCmd_work_fifo_ptr++ )
        {
            printf ( "%15s mtceCmd_workQ:%10s seq:%d stage:%d status [%d:%s]\n",
                    node_ptr->hostname.c_str(),
                    _get_cmd_str(node_ptr->mtcCmd_work_fifo_ptr->cmd).c_str(),
                    node_ptr->mtcCmd_work_fifo_ptr->seq,
                    node_ptr->mtcCmd_work_fifo_ptr->stage,
                    node_ptr->mtcCmd_work_fifo_ptr->status,
                    node_ptr->mtcCmd_work_fifo_ptr->status_string.c_str());
        }
    }
}

void nodeLinkClass::mtcCmd_doneQ_dump ( struct nodeLinkClass::node * node_ptr )
{
    if ( node_ptr->mtcCmd_done_fifo.size() != 0 )
    {
        for ( node_ptr->mtcCmd_done_fifo_ptr  = node_ptr->mtcCmd_done_fifo.begin() ;
              node_ptr->mtcCmd_done_fifo_ptr != node_ptr->mtcCmd_done_fifo.end();
              node_ptr->mtcCmd_done_fifo_ptr++ )
        {
            printf ( "%15s mtceCmd_doneQ:%10s seq:%d stage:%d status [%d:%s]\n",
                    node_ptr->hostname.c_str(),
                    _get_cmd_str(node_ptr->mtcCmd_done_fifo_ptr->cmd).c_str(),
                    node_ptr->mtcCmd_done_fifo_ptr->seq,
                    node_ptr->mtcCmd_done_fifo_ptr->stage,
                    node_ptr->mtcCmd_done_fifo_ptr->status,
                    node_ptr->mtcCmd_work_fifo_ptr->status_string.c_str());
        }
    }
}

void nodeLinkClass::mtcCmd_doneQ_dump_all ( void )
{
    struct node * ptr = static_cast<struct node *>(NULL) ;

    /* check for empty list condition */
    if ( head != NULL )
    {
        /* Now search the node list */
        for ( ptr = head ; ptr != NULL ; ptr = ptr->next )
        {
            mtcCmd_doneQ_dump ( ptr );
            mtcCmd_doneQ_purge ( ptr );
        }
    }
}

void nodeLinkClass::mtcCmd_workQ_dump_all ( void )
{
    struct node * ptr = static_cast<struct node *>(NULL) ;

    /* check for empty list condition */
    if ( head != NULL )
    {
        /* Now search the node list */
        for ( ptr = head ; ptr != NULL ; ptr = ptr->next )
        {
            mtcCmd_workQ_dump ( ptr );
        }
    }
}

int nodeLinkClass::cmd_handler ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;

    /* Should not be called empty but check just in case */
    if ( node_ptr->mtcCmd_work_fifo.size() == 0 )
        return (rc);

    node_ptr->mtcCmd_work_fifo_ptr = node_ptr->mtcCmd_work_fifo.begin ();
    switch ( node_ptr->mtcCmd_work_fifo_ptr->stage )
    {
        case MTC_CMD_STAGE__START:
        {
            dlog ("%s mtcCmd: %d:%d.%d\n",
                      node_ptr->hostname.c_str(),
                      node_ptr->mtcCmd_work_fifo_ptr->cmd,
                      node_ptr->mtcCmd_work_fifo_ptr->parm1,
                      node_ptr->mtcCmd_work_fifo_ptr->parm2);

             if ( node_ptr->mtcCmd_work_fifo_ptr->cmd == MTC_OPER__RESET_PROGRESSION )
             {
                 node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET_PROGRESSION_START ;
             }
             else if ( node_ptr->mtcCmd_work_fifo_ptr->cmd == MTC_OPER__HOST_SERVICES_CMD )
             {
                 node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__HOST_SERVICES_SEND_CMD ;
             }
             else if ( node_ptr->mtcCmd_work_fifo_ptr->cmd == MTC_OPER__MODIFY_HOSTNAME )
             {
                 send_hbs_command   ( node_ptr->hostname, MTC_CMD_DEL_HOST );
                 send_guest_command ( node_ptr->hostname, MTC_CMD_DEL_HOST );

                 node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__MODIFY_HOSTNAME_START ;
             }
             else
             {
                 slog ("%s Unsupported Mtce Command (%d)\n",
                           node_ptr->hostname.c_str(),
                           node_ptr->mtcCmd_work_fifo_ptr->cmd );

                 node_ptr->mtcCmd_work_fifo_ptr->status = FAIL_BAD_PARM ;
                 node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
             }
             break ;
        }

        case MTC_CMD_STAGE__HOST_SERVICES_SEND_CMD:
        {
            send_mtc_cmd ( node_ptr->hostname, node_ptr->host_services_req.cmd, MGMNT_INTERFACE );

            /* Start timer that waits for the initial command received response
             * There is no point in waiting for the longer host services
             * execution timeout if the far end is not even able to ACK the
             * initial test request. Bare in mind that the execution of the
             * host services command can take a while so its timeout is much
             * longer and polled for in the 3rd phase of this fsm but only
             * if we get an initial command ACK. */
            mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_CMD_RSP_TIMEOUT );

            /* change state to waiting for that initial ACK */
            node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__HOST_SERVICES_RECV_ACK ;
            break ;
        }
        case MTC_CMD_STAGE__HOST_SERVICES_RECV_ACK:
        {
            if ( mtcTimer_expired ( node_ptr->mtcCmd_timer ) )
            {
                node_ptr->mtcCmd_work_fifo_ptr->status =
                node_ptr->host_services_req.status = FAIL_NO_CMD_ACK ;
                node_ptr->host_services_req.status_string =
                node_ptr->host_services_req.name ;
                node_ptr->host_services_req.status_string.append (" ack timeout") ;

                dlog ("%s %s (rc:%d)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.status_string.c_str(),
                          node_ptr->host_services_req.status );

                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__DONE ;
            }
            else if ( node_ptr->host_services_req.ack )
            {
                /* get the host services timeout and add MTC_AGENT_TIMEOUT_EXTENSION
                 * seconds so that it is a bit longer than the mtcClient timeout */
                int timeout = daemon_get_cfg_ptr()->host_services_timeout ;
                    timeout += MTC_AGENT_TIMEOUT_EXTENSION ;

                dlog ("%s %s request ack (monitor mode)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.name.c_str());

                node_ptr->host_services_req.cmd = MTC_CMD_HOST_SVCS_RESULT ;
                node_ptr->mtcCmd_work_fifo_ptr->stage =
                MTC_CMD_STAGE__HOST_SERVICES_WAIT_FOR_RESULT ;
                mtcTimer_reset ( node_ptr->mtcCmd_timer );
                mtcTimer_start ( node_ptr->mtcCmd_timer,
                                 mtcTimer_handler,
                                 timeout );
            }
            else if ( node_ptr->host_services_req.cmd == node_ptr->host_services_req.rsp )
            {
                dlog ("%s %s request ack (legacy mode)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.name.c_str());

                // Upgrades that lock storage nodes can
                // lead to storage corruption if ceph isn't given
                // enough time to shut down.
                //
                // The following special case for storage node
                // lock forces a 90 sec holdoff for pre-upgrade storage
                // hosts ; i.e. legacy mode.
                //
                if ( is_storage(node_ptr) )
                {
                    ilog ("%s waiting for ceph OSD shutdown\n", node_ptr->hostname.c_str());
                    mtcTimer_reset ( node_ptr->mtcCmd_timer );
                    mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_LOCK_CEPH_DELAY );
                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__STORAGE_LOCK_DELAY ;
                }
                else
                {
                    node_ptr->mtcCmd_work_fifo_ptr->status =
                    node_ptr->host_services_req.status = PASS ;

                    node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
                }
            }
            break ;
        }
        case MTC_CMD_STAGE__STORAGE_LOCK_DELAY:
        {
            /* wait for the timer to expire before moving on */
            if ( mtcTimer_expired ( node_ptr->mtcCmd_timer ) )
            {
                ilog ("%s ceph OSD shutdown wait complete\n",
                          node_ptr->hostname.c_str());

                node_ptr->mtcCmd_work_fifo_ptr->status =
                node_ptr->host_services_req.status = PASS ;

                node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
            }
            break ;
        }
        case MTC_CMD_STAGE__HOST_SERVICES_WAIT_FOR_RESULT:
        {
            if ( mtcTimer_expired ( node_ptr->mtcCmd_timer ) )
            {
                node_ptr->mtcCmd_work_fifo_ptr->status =
                node_ptr->host_services_req.status = FAIL_TIMEOUT ;

                node_ptr->host_services_req.status_string =
                node_ptr->host_services_req.name ;
                node_ptr->host_services_req.status_string.append (" execution timeout") ;

                dlog ("%s %s (rc:%d)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.status_string.c_str(),
                          node_ptr->host_services_req.status );
            }
            else if ( node_ptr->host_services_req.rsp != MTC_CMD_HOST_SVCS_RESULT )
            {
                /* waiting for result response ... */
                break ;
            }
            else if ( node_ptr->host_services_req.status == PASS )
            {
                dlog ("%s %s completed\n",
                        node_ptr->hostname.c_str(),
                        node_ptr->host_services_req.name.c_str());

                node_ptr->mtcCmd_work_fifo_ptr->status = PASS ;
            }
            else
            {
                node_ptr->mtcCmd_work_fifo_ptr->status =
                node_ptr->host_services_req.status ;

                if ( ! node_ptr->host_services_req.status_string.empty() )
                {
                    wlog ("%s %s\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->host_services_req.status_string.c_str());
                }

                node_ptr->host_services_req.status_string =
                node_ptr->host_services_req.name ;
                node_ptr->host_services_req.status_string.append (" execution failed") ;

                dlog ("%s %s ; rc:%d\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->host_services_req.status_string.c_str(),
                          node_ptr->host_services_req.status);
            }
            node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
            break ;
        }
        /***************************************************************************
         *
         *                  'Reset Progression' Command Stages
         *
         * This target handler FSM is responsible for resetting a host through
         * progression escalation of interfaces. First a reboot by command is
         * attempted over the management network. If that fails the same operation
         * is tried over the cluster-host network. If both reboot command
         * attempts fail and the board management network for this host is
         * provisioned then reset through it is attempted.
         * Number of reset retries is specified in the command parameter 1
         * where a value of -1 means infinitely and a value of zero means no
         * retries ; only attempt up to all provisioned interfaces only once.
         *
         * *************************************************************************/
        case MTC_CMD_STAGE__RESET_PROGRESSION_START:
        {
            node_ptr->cmd_retries = 0 ;
            if ( node_ptr->cmd.task == true )
            {
                /* Management Reboot Failed */
                mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOT_REQUEST );
            }

            start_offline_handler ( node_ptr );

            node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__REBOOT ;
            break ;
        }
        case MTC_CMD_STAGE__REBOOT:
        {
            int rc = PASS ;
            bool send_reboot_ok = false ;

            node_ptr->reboot_cmd_ack_mgmnt = false ;
            node_ptr->reboot_cmd_ack_clstr = false ;

            /* send reboot command */
            node_ptr->cmdReq = MTC_CMD_REBOOT ;
            node_ptr->cmdRsp = MTC_CMD_NONE   ;
            if (( rc = send_mtc_cmd ( node_ptr->hostname,
                                      MTC_CMD_REBOOT,
                                      MGMNT_INTERFACE )) != PASS )
            {
                wlog ("%s reboot request failed (%s) (rc:%d)\n",
                       node_ptr->hostname.c_str(),
                       get_iface_name_str(MGMNT_INTERFACE), rc);
            }
            else
            {
                 send_reboot_ok = true ;
            }

            if ( clstr_network_provisioned == true )
            {
                if (( rc = send_mtc_cmd ( node_ptr->hostname,
                                          MTC_CMD_REBOOT,
                                          CLSTR_INTERFACE )) != PASS )
                {
                    wlog ("%s 'reboot' request failed (%s) (rc:%d)\n",
                        node_ptr->hostname.c_str(),
                        get_iface_name_str(CLSTR_INTERFACE), rc);
                }
                else
                {
                    send_reboot_ok = true ;
                }
            }

            if ( send_reboot_ok == true )
            {
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__REBOOT_ACK ;
                mtcTimer_reset ( node_ptr->mtcCmd_timer );
                mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_CMD_RSP_TIMEOUT );

                ilog ("%s waiting for REBOOT ACK\n", node_ptr->hostname.c_str() );
            }
            else
            {
                if ( node_ptr->cmd.task == true )
                {
                    /* Reboot Failed */
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOT_FAIL );
                }
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET ;
            }
            break ;
        }
        case MTC_CMD_STAGE__REBOOT_ACK:
        {
            /* can come in from either interface */
            if ( node_ptr->cmdRsp != MTC_CMD_REBOOT )
            {
                if ( node_ptr->mtcCmd_timer.ring == true )
                {
                    if ( node_ptr->cmd.task == true )
                    {
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOT_FAIL );
                    }
                    wlog ("%s REBOOT ACK Timeout\n", node_ptr->hostname.c_str());

                    node_ptr->mtcCmd_timer.ring = false ;

                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET ;
                }
            }
            else
            {
                /* declare successful reboot */
                plog ("%s REBOOT Request Succeeded\n", node_ptr->hostname.c_str());

                if ( node_ptr->cmd.task == true )
                {
                    /* Management Reboot Failed */
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOTING );
                }
                set_uptime ( node_ptr, 0 , false );

                /* start timer that verifies board has reset */
                mtcTimer_reset ( node_ptr->mtcCmd_timer );

                /* progress to RESET if we have tried 5 times already */
                if ( node_ptr->cmd_retries >= RESET_PROG_MAX_REBOOTS_B4_RESET )
                {
                    elog ("%s still not offline ; trying reset\n", node_ptr->hostname.c_str());
                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET ;
                }
                else
                {
                    int delay = (((offline_period*offline_threshold)/1000)+3);
                    ilog ("%s searching for offline ; next reboot attempt in %d seconds\n",
                              node_ptr->hostname.c_str(), delay);

                    /* After the host is reset we need to wait for it to stop sending mtcAlive messages
                     * Delay the time fo the offline handler to run to completion at least once before
                     * timing out and retrying the reset again */
                    mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, delay );

                    /* Wait for the host to go offline */
                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
                }
            }
            break ;
        }
        case MTC_CMD_STAGE__RESET:
        {
            if (( node_ptr->bmc_provisioned == true ) && ( node_ptr->bmc_accessible == true ))
            {
                plog ("%s Performing RESET over Board Management Interface\n", node_ptr->hostname.c_str());
                if ( node_ptr->cmd.task == true )
                {
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_RESET_REQUEST);
                }

                /* bmc power control reset by bmc */
                    rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_RESET );

                if ( rc == PASS )
                {
                    dlog ("%s Board Management Interface RESET Requested\n", node_ptr->hostname.c_str());

                    mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY );
                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET_ACK;
                    break ;
                }
                else
                {
                    node_ptr->mtcCmd_work_fifo_ptr->status = rc ;
                    wlog ("%s 'reset' command request failed (%d)\n", node_ptr->hostname.c_str(), rc );
                }
            }
            else
            {
                if ( node_ptr->bmc_provisioned == false )
                {
                    wlog ("%s Board Management Interface not provisioned\n", node_ptr->hostname.c_str());
                }
                else if ( node_ptr->bmc_accessible == false )
                {
                    wlog ("%s Board Management Interface not accessible\n", node_ptr->hostname.c_str());
                }
           }
            int delay = (((offline_period*offline_threshold)/1000)+3);
            mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, delay );
            node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
            break ;
        }
        case MTC_CMD_STAGE__RESET_ACK:
        {
             if ( node_ptr->mtcCmd_timer.ring == true )
             {
                  int delay = (((offline_period*offline_threshold)/1000)+3);

                  /* bmc power control reset by bmc */
                     rc = bmc_command_recv ( node_ptr );
                     if ( rc == RETRY )
                     {
                         mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY );
                         break ;
                     }

                  if ( rc )
                  {
                      elog ("%s Board Management Interface RESET Unsuccessful\n", node_ptr->hostname.c_str());
                      if ( node_ptr->cmd.task == true )
                      {
                          mtcInvApi_update_task ( node_ptr, MTC_TASK_RESET_FAIL);
                      }
                      mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, delay );
                      node_ptr->mtcCmd_work_fifo_ptr->status = rc ;
                      node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
                  }
                  else
                  {
                      plog ("%s Board Management Interface RESET Command Succeeded\n", node_ptr->hostname.c_str());

                      if (( node_ptr->adminAction != MTC_ADMIN_ACTION__RESET ) &&
                          ( node_ptr->adminAction != MTC_ADMIN_ACTION__REBOOT ))
                      {
                          mtcAlarm_log ( node_ptr->hostname, MTC_LOG_ID__COMMAND_AUTO_RESET );
                      }

                      set_uptime ( node_ptr, 0 , false );

                      if ( node_ptr->cmd.task == true )
                      {
                          mtcInvApi_update_task ( node_ptr, MTC_TASK_RESETTING );
                      }
                      mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, delay );
                      node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
                      ilog ("%s waiting for host to go offline (%d secs) before retrying reset\n",
                                node_ptr->hostname.c_str(),
                                delay);
                  }
             }
             break ;
        }
        case MTC_CMD_STAGE__OFFLINE_CHECK:
        {
            if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
            {
                mtcTimer_reset ( node_ptr->mtcCmd_timer );

                clear_service_readies ( node_ptr );

                qlog ("%s Reset Progression Complete ; host is offline (after %d retries)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->cmd_retries );
                node_ptr->mtcCmd_work_fifo_ptr->status = PASS ;
                node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
            }

            else if ( node_ptr->mtcCmd_timer.ring == true )
            {
                if ( ++node_ptr->cmd_retries < RESET_PROG_MAX_REBOOTS_B4_RETRY )
                {
                    ilog ("%s REBOOT (retry %d of %d)\n",
                              node_ptr->hostname.c_str(),
                              node_ptr->cmd_retries,
                              RESET_PROG_MAX_REBOOTS_B4_RETRY );

                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__REBOOT ;
                }
                else
                {
                    ilog ("%s still not offline\n", node_ptr->hostname.c_str());
                    node_ptr->mtcCmd_work_fifo_ptr->status = FAIL_RETRY ;
                    node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__RESET_PROGRESSION_RETRY ;
                }
            }
            break ;
        }
        case MTC_CMD_STAGE__RESET_PROGRESSION_RETRY:
        {
            /* Complete command if we reach max retries */
            if ( ++node_ptr->mtcCmd_work_fifo_ptr->parm2 > node_ptr->mtcCmd_work_fifo_ptr->parm1 )
            {
                plog ("%s Reset Progression Done\n", node_ptr->hostname.c_str());
                node_ptr->mtcCmd_work_fifo_ptr->status = FAIL_RETRY ;
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__DONE ;
            }
            else
            {
                wlog ("%s Reset Progression Retry\n", node_ptr->hostname.c_str());
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET_PROGRESSION_START ;
            }

            stop_offline_handler ( node_ptr );
            break ;
        }

        case MTC_CMD_STAGE__IPMI_COMMAND_SEND:
        {
            if ( bmc_command_send ( node_ptr, node_ptr->cmdReq ) != PASS )
            {
                elog ("%s IPMI %s Send Failed\n",
                          node_ptr->hostname.c_str(),
                          bmcUtil_getCmd_str(node_ptr->cmdReq).c_str());

                node_ptr->mtcCmd_work_fifo_ptr->status = FAIL_RETRY ;
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__DONE ;
            }
            else
            {
                plog ("%s IPMI %s Requested\n",
                          node_ptr->hostname.c_str(),
                          bmcUtil_getCmd_str(node_ptr->cmdReq).c_str());

                mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_IPMITOOL_REQUEST_DELAY );
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__IPMI_COMMAND_RECV ;
            }
            break ;
        }

        case MTC_CMD_STAGE__IPMI_COMMAND_RECV:
        {
            if ( mtcTimer_expired ( node_ptr->mtcCmd_timer ) )
            {
                rc = bmc_command_recv ( node_ptr );
                if ( rc == RETRY )
                {
                     mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_SECS_5 ) ;
                     break ;
                }
                else if ( rc == PASS )
                {
                    plog ("%s IPMI %s Successful\n", node_ptr->hostname.c_str(),
                                                     bmcUtil_getCmd_str(node_ptr->cmdReq).c_str());
                }
                else
                {
                    plog ("%s IPMI %s Requested\n", node_ptr->hostname.c_str(),
                                                    bmcUtil_getCmd_str(node_ptr->cmdReq).c_str());
                }
                node_ptr->mtcCmd_work_fifo_ptr->status = rc ;
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
            }
            break ;
        }

        /***************************************************************************
         *
         *                  'Modify Hostname' Command Stages
         *
         * *************************************************************************/
        case MTC_CMD_STAGE__MODIFY_HOSTNAME_START:
        {
            send_hbs_command   ( node_ptr->hostname, MTC_CMD_DEL_HOST );
            send_hwmon_command ( node_ptr->hostname, MTC_CMD_DEL_HOST );
            send_guest_command ( node_ptr->hostname, MTC_CMD_DEL_HOST );

            mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, work_queue_timeout );

            node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__MODIFY_HOSTNAME_DELETE_WAIT ;

            break ;
        }
        case MTC_CMD_STAGE__MODIFY_HOSTNAME_DELETE_WAIT:
        {
            /* We still doing enable work ? */
            if ( node_ptr->libEvent_work_fifo.size () == 0 )
            {
                string name = node_ptr->mtcCmd_work_fifo_ptr->name ;

                if ( node_ptr->mtcCmd_timer.tid )
                    mtcTimer_stop ( node_ptr->mtcCmd_timer );

                /* make the change */
                hostname_inventory.remove ( node_ptr->hostname );
                node_ptr->hostname = name ;
                hostname_inventory.push_back ( node_ptr->hostname );

                /* update the timer hostname */
                node_ptr->mtcTimer.hostname = name ;
                node_ptr->mtcAlive_timer.hostname = name ;
                node_ptr->mtcSwact_timer.hostname = name ;
                node_ptr->mtcCmd_timer.hostname = name ;
                node_ptr->oosTestTimer.hostname = name ;
                node_ptr->insvTestTimer.hostname = name ;
                node_ptr->mtcConfig_timer.hostname = name ;

                mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, work_queue_timeout );

                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__MODIFY_HOSTNAME_CREATE_WAIT ;

                /* return RETRY so that the FSM reloads the inventory loop */
                return (RETRY);
            }
            if ( node_ptr->mtcCmd_timer.ring == true )
            {
                elog ("%s mtcCmd timeout ; purging host's work queue\n", node_ptr->hostname.c_str());
                workQueue_purge ( node_ptr );
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__DONE ;
            }
            break ;
        }
        case MTC_CMD_STAGE__MODIFY_HOSTNAME_CREATE_WAIT:
        {
            /* We still doing create work ? */
            if ( node_ptr->libEvent_work_fifo.size() == 0 )
            {
                if ( node_ptr->mtcCmd_timer.tid )
                    mtcTimer_stop ( node_ptr->mtcCmd_timer );

                send_hbs_command   ( node_ptr->hostname, MTC_CMD_ADD_HOST );
                send_hwmon_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );
                send_guest_command ( node_ptr->hostname, MTC_CMD_ADD_HOST );

                if ( node_ptr->operState == MTC_OPER_STATE__ENABLED )
                {
                   send_hbs_command ( node_ptr->hostname, MTC_CMD_START_HOST );
                }
                node_ptr->mtcCmd_work_fifo_ptr->status = PASS          ;
                node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
            }
            if ( node_ptr->mtcCmd_timer.ring == true )
            {
                elog ("%s hostname change failed\n", node_ptr->hostname.c_str());
                elog ("... workQueue empty timeout ; purging host's work queue\n");
                workQueue_purge ( node_ptr );
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__DONE ;
            }
            break ;
        }
        case MTC_CMD_STAGE__DONE:
        case MTC_CMD_STAGE__STAGES:
        default:
        {
            int size ;

            mtcTimer_reset ( node_ptr->mtcCmd_timer );

            if ( node_ptr->mtcCmd_work_fifo_ptr->status != PASS )
            {
                qlog ("%s Command '%s' (%d) Failed (Status:%d)\n",
                          node_ptr->hostname.c_str(),
                          _get_cmd_str(node_ptr->mtcCmd_work_fifo_ptr->cmd).c_str(),
                                       node_ptr->mtcCmd_work_fifo_ptr->cmd,
                                       node_ptr->mtcCmd_work_fifo_ptr->status );
            }
            else
            {
                qlog ("%s Command '%s' Completed\n", node_ptr->hostname.c_str(),
                          _get_cmd_str(node_ptr->mtcCmd_work_fifo_ptr->cmd).c_str());
            }
            if ( ( size = node_ptr->mtcCmd_done_fifo.size()) != 0 )
            {
                wlog ( "%s mtcCmd doneQ not empty (contains %d elements)\n",
                           node_ptr->hostname.c_str(), size );
                mtcCmd_doneQ_purge ( node_ptr );
            }
            node_ptr->mtcCmd_done_fifo.push_front(node_ptr->mtcCmd_work_fifo.front());
            node_ptr->mtcCmd_work_fifo.pop_front();
            break ;
        }
    }
    return (PASS);
}

/* ***********************************************************************
 *
 * Name       : nodeLinkClass::mtcCmd_workQ_purge
 *
 * Description: Removes all items from the work queue.
 *
 */
int nodeLinkClass::mtcCmd_workQ_purge ( struct nodeLinkClass::node * node_ptr )
{
    int size = node_ptr->mtcCmd_work_fifo.size() ;
    if ( size )
    {
        wlog ("%s purging %d items from work queue\n", node_ptr->hostname.c_str(), size );
        for ( node_ptr->mtcCmd_work_fifo_ptr = node_ptr->mtcCmd_work_fifo.begin();
              node_ptr->mtcCmd_work_fifo_ptr != node_ptr->mtcCmd_work_fifo.end();
              node_ptr->mtcCmd_work_fifo_ptr++ )
        {
            wlog ("%s purging mtcCmd '%s' in stage %d from work queue\n",
                      node_ptr->hostname.c_str(),
                      _get_cmd_str(node_ptr->mtcCmd_work_fifo_ptr->cmd).c_str(),
                      node_ptr->mtcCmd_work_fifo_ptr->stage);
        }
        node_ptr->mtcCmd_work_fifo.clear();
    }
    else
    {
        qlog ("%s all work done\n", node_ptr->hostname.c_str());
    }
    return (PASS);
}


/* ***********************************************************************
 *
 * Name       : nodeLinkClass::mtcCmd_doneQ_purge
 *
 * Description: Removes all items from the mtcCmd done queue.
 *
 * Returns a failure, the sequence number of the first command
 * in the done queue that did not PASS.
 *
 */
int nodeLinkClass::mtcCmd_doneQ_purge ( struct nodeLinkClass::node * node_ptr )
{
    int rc = PASS ;
    int size = node_ptr->mtcCmd_done_fifo.size() ;
    if ( size )
    {
        int index = 0 ;
        for ( node_ptr->mtcCmd_done_fifo_ptr = node_ptr->mtcCmd_done_fifo.begin();
              node_ptr->mtcCmd_done_fifo_ptr != node_ptr->mtcCmd_done_fifo.end();
              node_ptr->mtcCmd_done_fifo_ptr++ )
        {
            index++ ;
            if ( node_ptr->mtcCmd_done_fifo_ptr->status )
            {
                dlog ("%s mtcCmd:%d failed (status:%d) (%d of %d)\n",
                        node_ptr->hostname.c_str(),
                        node_ptr->mtcCmd_done_fifo_ptr->cmd,
                        node_ptr->mtcCmd_done_fifo_ptr->status,
                        index, size);
                /* Save sequence of first failed command */
                if ( rc == PASS )
                {
                    rc = node_ptr->mtcCmd_done_fifo_ptr->seq ;
                }
            }
        }
        if ( rc == PASS )
        {
            dlog ("%s all (%d) mtcCmd operations passed\n", node_ptr->hostname.c_str(), size );
        }

        qlog ("%s purging %d items from done queue\n", node_ptr->hostname.c_str(), size );
        node_ptr->mtcCmd_done_fifo.clear();
    }
    return (rc);
}

