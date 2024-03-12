/*
 * Copyright (c) 2013-2017, 2023-2024 Wind River Systems, Inc.
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
            node_ptr->reboot_cmd_ack_pxeboot = false ;

            /* send reboot command */
            node_ptr->cmdReq = MTC_CMD_REBOOT ;
            node_ptr->cmdRsp = MTC_CMD_NONE   ;

            // Send the reboot command on all provisioned networks
            if ( this->pxeboot_network_provisioned == true )
            {
                if (( rc = send_mtc_cmd ( node_ptr->hostname,
                                          MTC_CMD_REBOOT,
                                          PXEBOOT_INTERFACE )) != PASS )
                {
                    // Don't report a warning log if the far end pxeboot
                    // network address is not learned yet.
                    if ( rc != FAIL_HOSTADDR_LOOKUP )
                    {
                        wlog ("%s reboot request failed (%s) (rc:%d)\n",
                                  node_ptr->hostname.c_str(),
                                  get_iface_name_str(PXEBOOT_INTERFACE), rc);
                    }
                    else
                    {
                        ilog ("%s %s network address not learned yet ; can't reboot",
                                  node_ptr->hostname.c_str(),
                                  get_iface_name_str(PXEBOOT_INTERFACE));
                    }
                }
                else
                {
                    send_reboot_ok = true ;
                }
            }

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
                    // Don't report a warning log if the far end cluster
                    // network IP is not learned yet.
                    if ( rc != FAIL_HOSTADDR_LOOKUP )
                    {
                        wlog ("%s reboot request failed (%s) (rc:%d)",
                                  node_ptr->hostname.c_str(),
                                  get_iface_name_str(CLSTR_INTERFACE), rc);
                    }
                    else
                    {
                        ilog ("%s %s network address not learned yet ; can't reboot",
                                  node_ptr->hostname.c_str(),
                                  get_iface_name_str(CLSTR_INTERFACE));
                    }
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

                ilog ("%s waiting for reboot ACK\n", node_ptr->hostname.c_str() );
            }
            else
            {
                /* This means that the mtcAgent can't send commands.
                 * Very unlikely case. Fail the operation.
                 */
                if ( node_ptr->cmd.task == true )
                {
                    /* Reboot Failed */
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOT_FAIL );
                }
                node_ptr->mtcCmd_work_fifo_ptr->status = FAIL_SOCKET_SENDTO  ;
                node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
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
                    if (( node_ptr->cmd.task == true ) && ( node_ptr->cmd_retries == 0 ))
                    {
                        /* no need to repost task on retries */
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOT_FAIL );
                    }
                    node_ptr->mtcCmd_timer.ring = false ;

                    /* progress to RESET if we have tried
                     * RESET_PROG_MAX_REBOOTS_B4_RESET times already */
                    if ( ++node_ptr->cmd_retries >= RESET_PROG_MAX_REBOOTS_B4_RESET )
                    {
                        wlog ("%s reboot ACK timeout ; max reboot retries reached",
                                  node_ptr->hostname.c_str());
                        if ( node_ptr->bmc_provisioned )
                        {
                            int reset_delay = bmc_reset_delay - (RESET_PROG_MAX_REBOOTS_B4_RESET * MTC_CMD_RSP_TIMEOUT);
                            node_ptr->bmc_reset_pending_log_throttle = 0 ;
                            gettime ( node_ptr->reset_delay_start_time );

                            /* Clear the counts so we can tell if we have been getting mtcAlive
                             * messages from the remote host during the reset delay window */
                            node_ptr->mtcAlive_mgmnt_count = 0 ;
                            node_ptr->mtcAlive_clstr_count = 0 ;
                            node_ptr->mtcAlive_pxeboot_count = 0 ;

                            wlog ("%s ... bmc reset in %d secs", node_ptr->hostname.c_str(), reset_delay);
                            mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, reset_delay );
                            node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET ;
                        }
                        else
                        {
                            ilog ("%s bmc not provisioned ; search for offline", node_ptr->hostname.c_str());
                            mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, offline_timeout_secs());
                            node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
                        }
                    }
                    else
                    {
                        int retry_delay = MTC_CMD_RSP_TIMEOUT ;
                        wlog ("%s reboot ACK timeout ; reboot retry (%d of %d) in %d secs",
                                      node_ptr->hostname.c_str(),
                                      node_ptr->cmd_retries,
                                      RESET_PROG_MAX_REBOOTS_B4_RESET-1,
                                      retry_delay);
                        mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, retry_delay );
                    }
                }
            }
            else
            {
                // log the acks
                string nwk_ack = "" ;
                if ( node_ptr->reboot_cmd_ack_pxeboot )
                     nwk_ack.append(get_iface_name_str(PXEBOOT_INTERFACE));
                if ( node_ptr->reboot_cmd_ack_mgmnt )
                {
                    if ( !nwk_ack.empty() )
                        nwk_ack.append(",");
                    nwk_ack.append(get_iface_name_str(MGMNT_INTERFACE));
                }
                if ( node_ptr->reboot_cmd_ack_clstr )
                {
                    if ( !nwk_ack.empty() )
                        nwk_ack.append(",");
                    nwk_ack.append(get_iface_name_str(CLSTR_INTERFACE));
                }

                /* declare successful reboot */
                plog ("%s reboot request succeeded (%s)", node_ptr->hostname.c_str(), nwk_ack.c_str());

                if ( node_ptr->cmd.task == true )
                {
                    /* Management Reboot Failed */
                    mtcInvApi_update_task ( node_ptr, MTC_TASK_REBOOTING );
                }
                set_uptime ( node_ptr, 0 , false );

                /* start timer that verifies board has reset */
                mtcTimer_reset ( node_ptr->mtcCmd_timer );

                /* progress to RESET if we have tried 5 times already */
                if ( ++node_ptr->cmd_retries >= RESET_PROG_MAX_REBOOTS_B4_RESET )
                {
                    int reset_delay = bmc_reset_delay - (RESET_PROG_MAX_REBOOTS_B4_RESET * MTC_CMD_RSP_TIMEOUT) ;
                    node_ptr->bmc_reset_pending_log_throttle = 0 ;
                    gettime ( node_ptr->reset_delay_start_time );

                    /* Clear the counts so we can tell if we have been getting mtcAlive
                     * messages from the remote host during the reset delay window */
                    node_ptr->mtcAlive_mgmnt_count = 0 ;
                    node_ptr->mtcAlive_clstr_count = 0 ;
                    node_ptr->mtcAlive_pxeboot_count = 0 ;

                    wlog ("%s max reboot retries reached ; still not offline ; reset in %3d secs",
                                  node_ptr->hostname.c_str(), reset_delay);
                    mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, reset_delay );
                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET ;
                }
                else
                {
                    ilog ("%s searching for offline ; next reboot attempt in %d seconds\n",
                              node_ptr->hostname.c_str(), offline_timeout_secs());

                    /* After the host is reset we need to wait for it to stop sending mtcAlive messages
                     * Delay the time fo the offline handler to run to completion at least once before
                     * timing out and retrying the reset again */
                    mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, offline_timeout_secs());

                    /* Wait for the host to go offline */
                    node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
                }
            }
            break ;
        }
        case MTC_CMD_STAGE__RESET:
        {
            if ( node_ptr->bmc_provisioned == true )
            {
                if ( node_ptr->mtcCmd_timer.ring == true )
                {
                    if ( node_ptr->bmc_accessible == true )
                    {
                        plog ("%s issuing reset over bmc", node_ptr->hostname.c_str());
                        if ( node_ptr->cmd.task == true )
                        {
                            mtcInvApi_update_task ( node_ptr, MTC_TASK_RESET_REQUEST);
                        }

                        /* bmc power control reset by bmc */
                        rc = bmc_command_send ( node_ptr, BMC_THREAD_CMD__POWER_RESET );
                        if ( rc == PASS )
                        {
                             ilog ("%s bmc reset requested", node_ptr->hostname.c_str());
                             mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                             node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__RESET_ACK;
                             break ;
                         }
                         else
                         {
                             node_ptr->mtcCmd_work_fifo_ptr->status = rc ;
                             wlog ("%s bmc reset command request failed (%d)", node_ptr->hostname.c_str(), rc );
                         }
                    }
                    else
                    {
                        wlog ("%s bmc not accessible ; unable to reset", node_ptr->hostname.c_str());
                    }
                }
                else
                {
                    /* To handle potentially large bmc_reset_delay values that could
                     * be longer than a boot time this check cancels the reset once the
                     * node goes online. Maybe the reset did get through or the node
                     * rebooted quite fast.
                     *
                     * However, don't allow momentary heartbeat loss recovery handling
                     * or the failure of just one (mgmnt or clstr) networks to mistakenly
                     * cancel the reset. Prevent the cancel if
                     * - the node uptime is high and
                     * - not receiving mtcAlive on any mtcAlive networks ;
                     *       mgmnt, clstr and pxeboot networks.
                     *
                     * Note: online does not mean both networks are receiving mtcAlive,
                     *       Currently just mgmnt needs to see mtcAlive for the node to
                     *       go online.
                     * TODO: Fix this in the future so both are required.
                     *       It came from the days when the cluster-host was named the
                     *       infrastructure network where at that time it was optional.
                     *       Cluster-host is no longer optional. */
                    if (( node_ptr->availStatus == MTC_AVAIL_STATUS__ONLINE ) &&
                        ( node_ptr->uptime < MTC_MINS_5 ) &&
                        ( node_ptr->mtcAlive_mgmnt_count ) &&
                        ( node_ptr->mtcAlive_clstr_count ) &&
                        ( node_ptr->mtcAlive_pxeboot_count ))
                    {
                        mtcTimer_reset ( node_ptr->mtcCmd_timer );
                        ilog ("%s cancelling reset ; host is online ; delay:%d uptime:%d mtcAlive:%d:%d:%d ",
                                  node_ptr->hostname.c_str(),
                                  bmc_reset_delay,
                                  node_ptr->uptime,
                                  node_ptr->mtcAlive_mgmnt_count,
                                  node_ptr->mtcAlive_clstr_count,
                                  node_ptr->mtcAlive_pxeboot_count);
                        node_ptr->mtcCmd_work_fifo_ptr->status = PASS ;
                        node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
                    }
                    else
                    {
                        time_debug_type now_time  ;
                        time_delta_type diff_time ;
                        int reset_delay = bmc_reset_delay - (RESET_PROG_MAX_REBOOTS_B4_RESET * MTC_CMD_RSP_TIMEOUT) ;
                        gettime ( now_time );
                        timedelta ( node_ptr->reset_delay_start_time, now_time, diff_time );
                        if ( reset_delay > diff_time.secs )
                        {
                            #define BMC_RESET_PENDING_LOG_THROTTLE (1000)
                            wlog_throttled ( node_ptr->bmc_reset_pending_log_throttle,
                                             BMC_RESET_PENDING_LOG_THROTTLE,
                                             "%s reset in %3ld secs ; delay:%d uptime:%d mtcAlive:%d:%d:%d",
                                             node_ptr->hostname.c_str(),
                                             reset_delay-diff_time.secs,
                                             bmc_reset_delay,
                                             node_ptr->uptime,
                                             node_ptr->mtcAlive_mgmnt_count,
                                             node_ptr->mtcAlive_clstr_count,
                                             node_ptr->mtcAlive_pxeboot_count);
                        }
                    }
                    break ; /* waiting path */
                }
            }
            else if ( node_ptr->bmc_provisioned == false )
            {
                wlog ("%s bmc not provisioned", node_ptr->hostname.c_str());
            }

            /* if we get here then either
             *  - the bmc is not proivisioned,
             *  - the bmc is not accessible after the bmc_reset_delay
             *  - the reset send command failed
             *  So we need to just jump to the offline check which will
             *  retry the reboot/reset if the host still does not go
             *  offline aftrer calculated delay
             */
            mtcTimer_reset ( node_ptr->mtcCmd_timer );
            mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, offline_timeout_secs());
            node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
            break ;
        }
        case MTC_CMD_STAGE__RESET_ACK:
        {
            if ( node_ptr->mtcCmd_timer.ring == true )
            {
                 /* bmc power control reset by bmc */
                 rc = bmc_command_recv ( node_ptr );
                 if ( rc == RETRY )
                 {
                     mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
                     break ;
                 }
                 else if ( rc )
                 {
                     elog ("%s bmc reset request failed [rc:%d]\n", node_ptr->hostname.c_str(), rc);
                     if ( node_ptr->cmd.task == true )
                     {
                        mtcInvApi_update_task ( node_ptr, MTC_TASK_RESET_FAIL);
                     }
                     node_ptr->mtcCmd_work_fifo_ptr->status = rc ;
                 }
                 else
                 {
                     plog ("%s bmc reset request succeeded\n", node_ptr->hostname.c_str());

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
                  }
                  ilog ("%s waiting for host to go offline ; %d secs before retrying reboot/reset",
                            node_ptr->hostname.c_str(), offline_timeout_secs());
                  node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__OFFLINE_CHECK ;
                  mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, offline_timeout_secs());
             }
             break ;
        }
        case MTC_CMD_STAGE__OFFLINE_CHECK:
        {
            if ( node_ptr->availStatus == MTC_AVAIL_STATUS__OFFLINE )
            {
                mtcTimer_reset ( node_ptr->mtcCmd_timer );

                clear_service_readies ( node_ptr );

                qlog ("%s reset progression complete ; host is offline (after %d retries)\n",
                          node_ptr->hostname.c_str(),
                          node_ptr->cmd_retries );
                node_ptr->mtcCmd_work_fifo_ptr->status = PASS ;
                node_ptr->mtcCmd_work_fifo_ptr->stage  = MTC_CMD_STAGE__DONE ;
            }

            else if ( node_ptr->mtcCmd_timer.ring == true )
            {
                if ( ++node_ptr->cmd_retries < RESET_PROG_MAX_REBOOTS_B4_RETRY )
                {
                    ilog ("%s reboot (retry %d of %d)\n",
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
                plog ("%s reset progression done\n", node_ptr->hostname.c_str());
                node_ptr->mtcCmd_work_fifo_ptr->status = FAIL_RETRY ;
                node_ptr->mtcCmd_work_fifo_ptr->stage = MTC_CMD_STAGE__DONE ;
            }
            else
            {
                wlog ("%s reset progression retry\n", node_ptr->hostname.c_str());
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

                mtcTimer_start ( node_ptr->mtcCmd_timer, mtcTimer_handler, MTC_BMC_REQUEST_DELAY );
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
                node_ptr->online_timer.hostname = name ;
                node_ptr->offline_timer.hostname = name ;
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

