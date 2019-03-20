#!/bin/bash
################################################################################
#
# Copyright (c) 2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
################################################################################
#
# Description: Displays memory usage information to check for memory leaks.
#
# Behaviour   : The script takes in a list of commands whose processes you want
#               monitored and then finds their process IDs, and uses that to find
#               their current Resident Set Size (RSS) using ps, and looks up their
#               Proportional Set Size (PSS) in /proc/<pid>/smaps. Only the initial
#               process run by the system is monitored; child processes and other
#               instances of these processes are ignored.
#
# This script is to be run on a controller node, and requires that it be run
# with sudo privileges or else it may not have access to /proc/<pid>/smaps for
# each of the desired processes.
#
# The script should be run with the following options:
# sudo ./memchk -t [#] --C [commands]
# Where following -t the time in seconds with which you want this script to repeat
# should be indicated. The default if no time is specified is 3600 seconds (1 hour).
# Following --C all arguments are viewed as commands you wish to be monitored.
# Each command should be separated by a space.
# e.g. sudo ./memchk -t 1800 --C command1 command2 command3
#
# Error logs can be found in /tmp/memchk_err.log
# Standard output can be found in /tmp/memchk_out.log
#
################################################################################

changeP=()
changeR=()
rss=()
firstP=()
lastP=()
firstR=()
lastR=()
leaking=()
leakFlag=0
flag=0
period=0
s1=() # Holds behaviour of most recent sample (does not change if present behaviour continues)
s2=() # Holds type of the previously observed behaviour that is different from the present behaviour
commands=()
trend=()
baseline=() # Sum of all RSS values for a given PID
count=() # Number of times RSS values have been sampled for a given PID
increasing=()
decreasing=()
stable=()
pattern=() # Stores a string indicating the present pattern


function trapCalled {
    echo $'\nReceived trap signal' >&2
    exit
}

trap trapCalled SIGHUP SIGINT SIGTERM

function helpMessage {
    echo "--------------------------------------------------------------------------------------"
    echo "Memory Leak And Information Tracking Tool"
    echo ""
    echo "Usage:"
    echo ""
    echo "sudo ./memchk.sh --C [commands]"
    echo ""
    echo " -t                       ... time in seconds with which to run this script. No time"
    echo "                              specified will result in the default of 3600s (1 hour)."
    echo " --C                      ... space delimited list of commands to monitor. This option"
    echo "                              must be the last one entered."
    echo " --help | -h              ... this info"
    echo ""
    echo "Note: This script must be run using sudo. If it is not, access to the memory information"
    echo "      of a given process may not be allowed by the system. PSS info is obtained from"
    echo "      /procs/<pid>/smaps"
    echo ""
    echo "      Error logs can be found in /tmp/memchk_err.log"
    echo "      Standard output can be found in /tmp/memchk_out.log"
    echo ""
    echo ""
    echo "Examples:"
    echo ""
    echo "sudo memchk -t 60 --C mtcClient mtcAgent            ... Check PSS and RSS values of the processes belonging to mtcClient"
    echo "                                                        and mtcAgent every 60 seconds (1 minute)"
    echo "sudo memchk -t 3600 --C pmond hwmond                ... Check PSS and RSS values of pmond, and hwmond every 3600s (1h)"
    echo "sudo memchl --C pmond hwmond                        ... Check PSS and RSS values of commands using default period of 3600s (1h)"
    echo "--------------------------------------------------------------------------------------"
    exit 0
}

# Prints information on suspected leaking process
function memLeak {
    printf "\n" >&2
    printf '%0.1s' "*"{1..150} >&2
    # Iterates over all keys in the array.
    for proc in ${!leaking[@]}
    do
        printf "\nPossible mem leak in: %s PID: %s Current RSS: %s Orig RSS: %s Current PSS: %s Orig PSS: %s\n" \
        ${leaking[proc]} $proc ${rss[proc]} ${firstR[proc]} ${lastP[proc]} ${firstP[proc]} >&2
    done
    printf '%0.1s' "*"{1..150} >&2
    printf "\n" >&2
}

if [ $UID -ne 0 ]; then
    echo $'\nWarning: Memchk must be run as \'root\' user to access PSS memory information'
    echo $'Use the -h option for help\n'
    exit 1
fi

if [ $# -eq 0 ]; then
    echo $'\nNo commands specified\nPlease try again and enter a command whose memory you would like to monitor'
    echo $'Use the -h option for help\n'
    exit 1
fi

exec > >(tee /tmp/NEWmemchk_out.log) 2> >(tee /tmp/NEWmemchk_err.log >&2)

# Cycles through commandline arguments to make sure valid input was received and
# to assign values correctly
while [[ $# > 0 ]]; do
    key="$1"

    case $key in

        # To make this more user-friendly, instead of having the user enter the period in seconds, consider using
        # 'shopt -s extglob' and 'if [[ $2 = +([0-9])m ]];' to check if the user entered 15m as a period to indicate 15 minutes etc.
        # Modify the regex for seconds and hours as well, then multiply value as necessary to convert into seconds for script
        -t)
        period="$2"
        shift
        ;;

        --C)
        shift
        if [ "$#" -eq "0" ]; then
            printf "Error: No commands specified.\n"
            exit 1
        fi
        for c in "$@"; do
            commands+=("$1")
            shift
        done
        ;;

        -h|--help)
        helpMessage
        ;;

        *)
        printf "\nUnknown argument passed: %s\n" $key
        printf "Use the -h option for help\n"
        exit 1
    esac
    shift
done

# Makes sure period has a positive value
if [ "$period" -le "0" ]; then
    period=3600
    printf "You have entered an invalid period. Period has been set to 3600 seconds.\n"
# The rate of kB/h has been hard-coded into the table, if values greater than or equal to 1 hour are used, the table
# will not show an accurate representation in the change in usage over time. There are various accuracy issues in
# modifying the code to display data to match your chosen period. Consider this and modify accordingly.
elif [ "$period" -lt "3600" ]; then
    printf "\nWARNING: You have chosen a period that is less than 1 hour. The rate of change in the table is displayed in kB/h, keep this in mind when reviewing results.\n"
fi

while true; do
    # Prints header for columns
    printf "\n%15s | %8s | Leak | %10s | %13s | %8s | %8s | %8s | %13s | %8s | %8s | %8s | Period: %-${#period}ss\n" \
            "Cmd" "PID" "Trend" "Change in RSS" "RSS" "Orig RSS" "Prev RSS"  "Change in PSS" "PSS" "Orig PSS" "Prev PSS" "$period" >&1
    padding=$(printf '%0.1s' "-"{1..180})
    printf '%*.*s' 0 $((156 + ${#period} )) "$padding" # Prints line of hyphens of variable size depending on the number of characters in period.
    # Cycles through each of the originally entered commands. This list does not change.
    for cmd in ${commands[@]}
    do
        # Finds all the PIDs associated with each command (commands may have more than one instance)
        procs="$(pgrep $cmd)"

        # The number of processes may change on each loop. Keep this in mind if expanding or reusing this script.
        for pid in ${procs[@]}
        do
            # In smaps the PSS value is located 3 lines below the line containing the process name. This works by setting
            # the awk variable comm to contain the same value as cmd, the file is then searched for the string pattern
            # contained in comm (cmd) and the PSS value associated with each instance of comm is summed and then printed.
            pss=$(awk -v comm="$cmd" '$0 ~ comm {getline;getline;getline;sum += $2;} END {print sum}' /proc/"$pid"/smaps)
            # obtains the RSS value of the indicated process
            rssCurrent=$(ps -p "$pid" --no-header -o rss)
            lastR[pid]="${rss[pid]}"

            # Child processes may exist ephemerally, as a result they may be added to our list of PIDs, but no longer
            # exist when we try to read their associated files in /proc/. This makes sure the file exists and that the
            # parent process is 1. If the parent process ID is not 1 then the process in question is a child proceess
            # and we do not care about its memory usage (for the purposes of this specific script). The continue
            # statement will return us to the for-loop and begin running for the next pid.
            if [ -f "/proc/$pid/status" ] && [ "$(awk '$0 ~ "PPid:" {print $2}' /proc/"$pid"/status)" -ne "1" ];then
                continue;
            fi

            # This checks that neither rssCurrent nor pss have empty values due to a child process being generated
            # and then killed off before its values could be read. Root occasionally generates a child process of
            # one of the monitored commands so the above if-statement doesn't exclude it because the PPID is 1.
            if [ -z "$rssCurrent" ] || [ -z "$pss" ]; then
                continue;
            fi

            # Sets initial values for PSS and RSS. NA is set instead of 0 because using numbers could lead to false
            # or inaccurate information. It also previously allowed one to see when child processes were spawned.
            if [ "$flag" -ne "1" ]; then
                firstP[pid]="$pss"
                lastP[pid]="NA"
                rss[pid]="$rssCurrent"
                firstR[pid]="${rss[pid]}"
                lastR[pid]="NA"
                s1[pid]=""
                s2[pid]=""
                trend[pid]=0
                increasing[pid]=0
                decreasing[pid]=0
                stable[pid]=0
                count[pid]=0
                baseline[pid]=0
            fi

            # In the event of a memory leak (the RSS value increasing), an X is placed in the 'Leak' column of the
            # printed table. The PID of the process is also added to an array to be sent to the memLeak function
            # once all of the commands' processes have been checked. A flag indicating that a possible leak has
            # been detected is also set.
            if [ "${rss[pid]}" -lt "$rssCurrent" ]; then
                lastR[pid]="${rss[pid]}"
                rss[pid]="$rssCurrent"
                leaking[pid]="$cmd"
                leak[pid]="X"
                let leakFlag=1
            fi

            # Calculates the changes in PSS and RSS usage over time. If this is the first run and there is no
            # previous value with which to compare against, delta is set to 0, where delta is the change over
            # time.
            if [ "${lastP[pid]}" = "NA" ]; then
                changeP[$pid]=0; deltaP=0.000;
            else
                changeP[pid]="$((changeP[$pid] + $pss - lastP[$pid]))";
                deltaP=$(awk -v chP="${changeP[$pid]}" -v hrs="${hours}" -v t="${period}" 'BEGIN {printf "%.3f", (chP/(hrs*t))*3600; exit(0)}');
            fi

            if [ "${lastR[pid]}" = "NA" ]; then
                changeR[$pid]=0; deltaR=0.000;
            else
                changeR[pid]="$((changeR[$pid] + rss[$pid] - lastR[$pid]))";
                deltaR=$(awk -v chR="${changeR[$pid]}" -v hrs="${hours}" -v t="${period}" 'BEGIN {printf "%.3f", (chR/(hrs*t))*3600; exit(0)}');
            fi

            # The below if-else block seeks to determine gradual sustained patterns of RSS usage over time to determine if the memory usage is gradually
            # increasing throughout the lifespan of the process (possible memory leak) or not. Non-gradual usage changes can be due to dynamic reallocation
            # and such 'eratic' behaviour is not indicative of any overall trends.
            # NOTE: If you would like to do this properly and determine whether or not such patterns exist by evaluating the RSS usage accross the entire
            #       lifespan of the script, consider the following method:
            #       Take the (RSS, time) value-pairs and make an augmented matrix and then use Gaussian elimination to solve the matrix and use the remaining
            #       values as the coefficients to create a least-squares parabola, which you can then find the first derivative of to determine the rate of
            #       change -- which will indicate increasing or decreasing behaviour at your current point relative to nearby datapoints and the behaviour
            #       of the rest of your graph (function).
            #       To do this consider using the python package numpy for matrix math and for derivatives. The issue with this method is finding a way to
            #       pipe data to a python script and have it return to the bash script. Because piping is usually done asynchronously, you may find that there
            #       are issues with having the values returned and printed properly in the bash script. This math can also be done in bash, but will take
            #       considerable effort.
            #---------------------------------------------------------------------------------------------------------------------------------------------------
            # This part checks to see that an established trend is being maintained.
            # It first checks that trend[pid] is greater than or equal to 3 because the else-block below increments the trend number such that when the same
            # pattern e.g. an increase in RSS that exceeds the baseline average occurs 3 times in a row, it establishes that there exists a trend of RSS increasing.
            # The existence of a pattern or 'trend' is gauged by whether the same behaviour has occured 3 times in a row, and that it continues to occur without
            # the opposite behaviour happening. For example, if a trend of increasing RSS has been observed, and on the next sample the RSS value is found to
            # be below the baseline average, this indicates that a decrease has occured and thus the trend has been broken, and a new trend must be established.
            # If the current trend is either increasing or decreasing, the RSS value can be equal to the baseline average for two consecutive samples without
            # the trend being broken. However, if the RSS value is equal to the baseline average for 3 consecutive samples, this indicates a new trend of the
            # RSS value reaching a stable value, and the current trend of increasing or decreasing is broken; that is to say, only a trend of 'stable' will be
            # permitted; if an increase or decrease is observed, all temporary and trend values will be reset, and the code will enter the else-block below and
            # attempt to establish a new trend from scratch. The reason for this behaviour is that trends are determined by observing a behaviour three
            # consecutive times, if a stable behaviour starts turning into an increasing or decreasing behaviour, the else-block is entered to wait for a new
            # behaviour to be established.
            # A trend cannot change immediately from increasing to decreasing. This is done to avoid representing erratic behaviour as a long-term pattern.
            # An increasing or decreasing trend must change to 'none' -- no trend observed -- before the opposite trend can be declared.
            # The baseline average is the RSS values for a PID from each sample added together and divided by the number of samples that have taken place.
            let count[pid]+=1
            let baseline[pid]+="$rssCurrent"
            avg=$(awk -v b="${baseline[pid]}" -v c="${count[pid]}" 'BEGIN {printf "%.0f", (b/c); exit(0)}')
            if [ "${trend[pid]}" -ge "3" ]; then
                if [ "${rss[pid]}" -gt "$avg" ] && ([ "${s1[pid]}" = "increasing" ] || ([ "${s1[pid]}" != "decreasing" ] && [ "${s2[pid]}" != "decreasing" ]) && [ "${stable[pid]}" -ne "3" ]); then
                    if [ "${s1[pid]}" != "increasing" ]; then
                        s2[pid]="${s1[pid]}"
                        s1[pid]="increasing"
                    fi
                elif [ "${rss[pid]}" -eq "$avg" ]; then
                    if [ "${s1[pid]}" != "stable" ]; then
                        stable[pid]=0
                        s2[pid]="${s1[pid]}"
                        s1[pid]="stable"
                    fi
                    let stable[pid]+=1
                    let stable[pid]+=1
                elif [ "${rss[pid]}" -lt "$avg" ] && ([ "${s1[pid]}" = "decreasing" ] || ([ "${s1[pid]}" != "increasing" ] && [ "${s2[pid]}" != "increasing" ]) && [ "${stable[pid]}" -ne "3" ]); then
                    if [ "${s1[pid]}" != "decreasing" ]; then
                        s2[pid]="${s1[pid]}"
                        s1[pid]="decreasing"
                    fi
                else
                    s1[pid]=""
                    s2[pid]=""
                    trend[pid]=0
                    increasing[pid]=0
                    decreasing[pid]=0
                    stable[pid]=0
                fi
            # This else-block is used to establish whether or not a trend has been established. It waits for a pattern of the RSS value of a PID to increase,
            # decrease, or remain stable relative to the baseline average three times in a row before it will declare that a trend exists. This is to avoid
            # viewing erratic increases and decreases in RSS as gradual increases or decreases in the system's (process') RSS usage.
            else
                if [ "${count[pid]}" -gt "0" ]; then
                    if [ "${rss[pid]}" -gt "$avg" ]; then
                        let trend[pid]+=1
                        let increasing[pid]+=1
                        s1[pid]="increasing"
                    elif [ "${rss[pid]}" -eq "$avg" ]; then
                        let trend[pid]+=1
                        let stable[pid]+=1
                        s1[pid]="stable"
                    elif [ "${rss[pid]}" -lt "$avg" ]; then
                        let trend[pid]+=1
                        let decreasing[pid]+=1
                        s1[pid]="decreasing"
                    fi
                    if [ "${increasing[pid]}" -gt "0" ] && [ "${decreasing[pid]}" -gt "0" ]; then
                        increasing[pid]=0
                        decreasing[pid]=0
                        stable[pid]=0
                        trend[pid]=0
                    fi
                fi
            fi

            if [ "${trend[pid]}" -ge "3" ]; then
                pattern[pid]="${s1[pid]}";
            else
                pattern[pid]="none";
            fi # Sets the trend variable for printing if a trend exists
            printf "\n%15s | %8s |  %2s  | %10s | %8s kB/h | %8s | %8s | %8s | %8s kB/h | %8s | %8s | %8s |" \
                $cmd $pid "${leak[pid]}" "${pattern[pid]}" $deltaR ${rss[pid]} ${firstR[pid]} ${lastR[pid]} $deltaP $pss ${firstP[pid]} ${lastP[pid]} >&1
            lastP[pid]="$pss"
            leak[pid]="" # Resets the indicator in the 'Leak' column
        done
    done

    if [ "$leakFlag" -eq "1" ]; then
        memLeak leaking[@];
    fi # Calls the mem leak function if flag is set
    unset leaking[@] # Clear the array holding PIDs of processes with potential leaks
    let leakFlag=0
    let hours+=1 # Hour count[pid]er used in calculating delta
    let flag=1 # Flag indicating that first run has completed so we no longer have to set values of 'NA'
    echo $'\n'
    sleep "$period"
done
