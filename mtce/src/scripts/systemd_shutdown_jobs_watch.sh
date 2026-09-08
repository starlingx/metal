#!/bin/bash
######################################################################
# Copyright (c) 2026 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
######################################################################
#
# systemd_shutdown_jobs_watch.sh
#
# Samples systemd busyness/state at a configurable interval and appends to
# /var/log/systemd_shutdown_jobs.log. Launched by mtcClient (via systemd-run)
# when a reboot/reset is received, so it runs as a systemd transient unit and
# keeps sampling through the shutdown transition even after mtcClient exits.
#
# Purpose: correlate with mtcClient.log by timestamp to see what systemd was
# doing (state + queued job count + the actual jobs) during a shutdown, e.g.
# while a 'systemctl stop pmon' call is stalled.
#
# Install path: /usr/local/sbin/systemd_shutdown_jobs_watch
# Output:       /var/log/systemd_shutdown_jobs.log  (persists across reboot)
#
# The script rotates its own log on startup (no dependency on logrotate, which
# does not run through a shutdown). Each run starts a fresh
# systemd_shutdown_jobs.log ; the previous run becomes systemd_shutdown_jobs.log.1
# and the one before that systemd_shutdown_jobs.log.2 (KEEP_LOGS generations).
#
# mtcClient launches it as a transient unit with properties that let it keep
# logging as far into the shutdown transition as possible WITHOUT stalling the
# shutdown itself:
#   systemd-run --unit=mtc-shutdown-jobs-watch \
#       -p DefaultDependencies=no \    # not stopped early in the shutdown txn
#       -p Before=shutdown.target \    # stopped as late as possible
#       -p TimeoutStopSec=1 \          # must exit within 1s ; never delays shutdown
#       -p KillMode=mixed \            # prompt, clean termination on stop
#       /usr/local/sbin/systemd_shutdown_jobs_watch
#
# To reduce noise, only per-job deltas are logged: "+add" when a job first
# appears, "chg" on a type/state change, and "-done" when it leaves the queue.
# The "-done" line reports how long the job was queued as "(held Ns)".
#
# Jobs already queued when the watcher starts are logged as "seen ...
# (pre-existing)" rather than "+add", since their real enqueue time was not
# observed ; their completion is reported as "(held >=Ns, pre-existing)" to
# mark the duration as a lower bound. Jobs still queued when the node reboots
# get no "-done" line (in-flight at power-off).
#
# When nothing changes, a compact "(unchanged)" heartbeat is emitted every
# HEARTBEAT_SECS seconds (arg 2, default 30 ; 0 disables it).
#
# Args: [interval_secs] [heartbeat_secs]
#
# Manual use (same properties recommended):
#   sudo systemd-run --unit=mtc-shutdown-jobs-watch \
#       -p DefaultDependencies=no -p Before=shutdown.target \
#       -p TimeoutStopSec=1 -p KillMode=mixed \
#       /usr/local/sbin/systemd_shutdown_jobs_watch [interval_secs] [heartbeat_secs]
#   sudo systemctl stop mtc-shutdown-jobs-watch

# Default seconds between "(unchanged)" heartbeats when nothing changes.
# A heartbeat keeps a long stall visible and confirms the watcher is alive.
# Set to 0 to disable heartbeats entirely (only state/job changes logged).
readonly DEFAULT_HEARTBEAT_SECS=30

# Number of previous log generations to retain (in addition to the current
# one). With KEEP=2 the script keeps: systemd_shutdown_jobs.log (current),
# systemd_shutdown_jobs.log.1, systemd_shutdown_jobs.log.2. Older are removed.
readonly KEEP_LOGS=2

OUT=/var/log/systemd_shutdown_jobs.log
INTERVAL="${1:-1}"   # seconds between samples (default 1)
HEARTBEAT_SECS="${2:-$DEFAULT_HEARTBEAT_SECS}"

ts() { date '+%Y-%m-%dT%H:%M:%S.%3N'; }

# Self-managed log rotation so each run starts a fresh file without relying on
# logrotate (which will not run through a shutdown). Rotate on startup:
#   drop the oldest, shift each generation up by one, then move current to .1.
# Result after rotation: <current> is fresh, .log.1 is the previous run, and
# up to KEEP_LOGS generations are retained (.log, .log.1, .log.2).
rotate_logs() {
    # Remove the oldest generation if present.
    rm -f "${OUT}.${KEEP_LOGS}"

    # Shift .N-1 -> .N down to .1 -> .2 .
    local i
    for (( i = KEEP_LOGS - 1; i >= 1; i-- )) ; do
        if [ -f "${OUT}.${i}" ] ; then
            mv -f "${OUT}.${i}" "${OUT}.$(( i + 1 ))"
        fi
    done

    # Move the current log to .log.1 so this run starts clean.
    if [ -f "$OUT" ] ; then
        mv -f "$OUT" "${OUT}.1"
    fi
}

rotate_logs

echo "$(ts) ===== systemd_shutdown_jobs_watch started (interval=${INTERVAL}s pid=$$) =====" > "$OUT"

# Track previous sample so we only log deltas.
PREV_STATE=""
PREV_NJOBS=""
LAST_HEARTBEAT=0
FIRST_SAMPLE=1         # 1 until the first job sample has been processed
declare -A PREV_JOB    # unit -> "type state" from the previous sample
declare -A JOB_START   # unit -> epoch (with ms) when the job was first seen
declare -A JOB_PREEXIST # unit -> 1 if the job was already queued at watcher start

# Epoch seconds with millisecond fraction, e.g. 1730000000.123
now_ms() { date '+%s.%3N'; }

while true ; do
    NOW=$(date +%s)
    STATE=$(systemctl is-system-running 2>/dev/null)
    NJOBS=$(systemctl show -p NJobs --value 2>/dev/null)
    JOBS=$(systemctl list-jobs --no-legend 2>/dev/null)

    STATE=${STATE:-unknown}
    NJOBS=${NJOBS:-?}

    # Build the current job map: unit -> "type state". list-jobs columns are:
    #   JOB UNIT TYPE STATE
    declare -A CUR_JOB=()
    if [ -n "$JOBS" ] ; then
        while read -r jid unit jtype jstate _rest ; do
            [ -z "$unit" ] && continue
            CUR_JOB["$unit"]="$jtype $jstate"
        done <<< "$JOBS"
    fi

    ANY_CHANGE=0

    # Log a header line only when state or the job count changes.
    if [ "$STATE" != "$PREV_STATE" ] || [ "$NJOBS" != "$PREV_NJOBS" ] ; then
        echo "$(ts) state=${STATE} njobs=${NJOBS}" >> "$OUT"
        ANY_CHANGE=1
    fi

    NOW_MS=$(now_ms)

    # Emit per-job deltas: added, changed (type/state), and removed. This keeps
    # the log to a few lines per second instead of re-dumping the whole queue.
    # On +add, record the first-seen time ; on -done, report elapsed time in
    # the queue. Jobs still present when the node reboots simply never get a
    # -done line (they were in-flight at power-off).
    for unit in "${!CUR_JOB[@]}" ; do
        if [ -z "${PREV_JOB[$unit]+set}" ] ; then
            JOB_START["$unit"]="$NOW_MS"
            if [ "$FIRST_SAMPLE" -eq 1 ] ; then
                # Job was already queued when the watcher started ; we did not
                # observe its real enqueue, so its held time is a lower bound.
                JOB_PREEXIST["$unit"]=1
                echo "$(ts)   job seen : ${unit} ${CUR_JOB[$unit]} (pre-existing)" >> "$OUT"
            else
                echo "$(ts)   job +add : ${unit} ${CUR_JOB[$unit]}" >> "$OUT"
            fi
            ANY_CHANGE=1
        elif [ "${PREV_JOB[$unit]}" != "${CUR_JOB[$unit]}" ] ; then
            echo "$(ts)   job chg  : ${unit} ${PREV_JOB[$unit]} -> ${CUR_JOB[$unit]}" >> "$OUT"
            ANY_CHANGE=1
        fi
    done
    for unit in "${!PREV_JOB[@]}" ; do
        if [ -z "${CUR_JOB[$unit]+set}" ] ; then
            # Compute time-in-queue if we recorded a start for this unit.
            # For pre-existing jobs (queued before the watcher started) the
            # value is a lower bound, flagged with a leading '>='.
            if [ -n "${JOB_START[$unit]+set}" ] ; then
                held=$(awk -v a="${JOB_START[$unit]}" -v b="$NOW_MS" \
                           'BEGIN { printf "%.3f", b - a }')
                if [ -n "${JOB_PREEXIST[$unit]+set}" ] ; then
                    echo "$(ts)   job -done: ${unit} ${PREV_JOB[$unit]} (held >=${held}s, pre-existing)" >> "$OUT"
                else
                    echo "$(ts)   job -done: ${unit} ${PREV_JOB[$unit]} (held ${held}s)" >> "$OUT"
                fi
                unset 'JOB_START[$unit]'
                unset 'JOB_PREEXIST[$unit]'
            else
                # No recorded start (should not normally happen) ; report
                # completion without a duration rather than a bogus one.
                echo "$(ts)   job -done: ${unit} ${PREV_JOB[$unit]} (held unknown)" >> "$OUT"
            fi
            ANY_CHANGE=1
        fi
    done

    if [ "$ANY_CHANGE" -eq 1 ] ; then
        LAST_HEARTBEAT="$NOW"
    elif [ "$HEARTBEAT_SECS" -gt 0 ] && [ $(( NOW - LAST_HEARTBEAT )) -ge "$HEARTBEAT_SECS" ] ; then
        # No change ; emit a compact heartbeat so a stall is still observable.
        echo "$(ts) state=${STATE} njobs=${NJOBS} (unchanged)" >> "$OUT"
        LAST_HEARTBEAT="$NOW"
    fi

    # Roll current -> previous for the next iteration.
    PREV_STATE="$STATE"
    PREV_NJOBS="$NJOBS"
    unset PREV_JOB ; declare -A PREV_JOB
    for unit in "${!CUR_JOB[@]}" ; do
        PREV_JOB["$unit"]="${CUR_JOB[$unit]}"
    done
    FIRST_SAMPLE=0

    sleep "$INTERVAL"
done
