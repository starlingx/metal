###################################################################################
#
# This file contains the sensor profile for the following board
#
#    "ProLiant DL380 Gen9" with "iLO 4 Standard"
# 
###################################################################################
#
# Please refer to sensor_integration_profile.README for a detailed
# explaination of the format and heirarchy of this file and how to
# develop a file like this for sensor integration for new servers.
#
####################################################################################

[SERVER]

targets  = Targets:system1:map1
name_cmd = show /system1 name
bmc_cmd  = show /system1 name
name     = ProLiant DL380 Gen9
bmc      = iLO 4 Standard
info     = show /system1 name
dump_cmd = show /system1 name    # show -a

group_types = COOLING,POWER,TEMPERATURE

[LOGS]
show_brief = show /system1/log1
show_detail = show -a /system1/log1

[INFO]
cmds = 7
cmd1 = show /system1/bootconfig1
cmd2 = show -a /system1/bootconfig1
cmd3 = show -a /system1/firmware1
cmd4 = show -a /system1/memory*
cmd5 = show -a /system1/swid*
cmd6 = show -a /system1/cpu*
cmd7 = show -a /system1/slot*

[CMDS]
reset = reset /system1
poweron = start /system1
poweroff = stop /system1
powerquery = show /system1

##################################################################################
#
# COOLING sensor management
# -----------------------------
#
##################################################################################

[COOLING]
groups = FANS1

[FANS1]
group = server fans

# sensor attributes for this group
sensortype = fan
datatype   = discrete      
interval   = 100       ; seconds
unit       = RPM

cmd = show -a /system1/fan*

ignore = na
pass = ok
minor = na
major = Bad
critical = critical

#detail = show -a /system1/fan*
#health_label = HealthState
#status_label = OperationalStatus

health = HealthState:Ok,na,Bad,Failed
status = OperationalStatus:Ok,na,Bad,Failed

sensors = fan1,fan2,fan3,fan4,fan5,fan6,fan7,fan8

[fan1]
name = Fan Block 1
cmd = show /system1/fan1
[fan2]
name = Fan Block 2
cmd = show /system1/fan2
[fan3]
name = Fan Block 3
cmd = show /system1/fan3
[fan4]
name = Fan Block 4
cmd = show /system1/fan4
[fan5]
name = Fan Block 5
cmd = show /system1/fan5
[fan6]
name = Fan Block 6
cmd = show /system1/fan6
[fan7]
name = Fan Block 7
cmd = show /system1/fan7
[fan8]
name = Fan Block 8
cmd = show /system1/fan8

##################################################################################
#
# TEMPERATURE sensor management
# -----------------------------
#
##################################################################################

[TEMPERATURE]
groups = TEMPERATURE1

[TEMPERATURE1]
group = server temperature

cmd = show -a /system1/sensor*

sensortype = temperature
datatype   = analog      
interval   = 30             ; seconds
unit       = Celsius

health = HealthState:Ok,na,Bad,Failed
status = OperationalStatus:Ok,na,Bad,Failed

sensors = sensor1,sensor2,sensor3,sensor4,sensor5,sensor6,sensor7,sensor8,sensor9,sensor10,sensor11,sensor12,sensor13,sensor14,sensor15,sensor16,sensor17,sensor18,sensor19,sensor20,sensor21,sensor22,sensor23,sensor24,sensor25,sensor26,sensor27,sensor28,sensor29,sensor30,sensor31,sensor32,sensor33,sensor34,sensor35,sensor36,sensor37,sensor38,sensor39,sensor40,sensor41,sensor42

upper_minor = 38
upper_major = 42
upper_fatal = 46

[sensor1]
name = 01-Inlet Ambient 
[sensor2]
name = 02-CPU 1 
[sensor3]
name = 03-CPU 2 
[sensor4]
name = 04-P1 DIMM 1-6 
[sensor5]
name = 05-P1 DIMM 7-12 
[sensor6]
name = 06-P2 DIMM 1-6 
[sensor7]
name = 07-P2 DIMM 7-12 
[sensor8]
name = 08-P1 Mem Zone 
[sensor9]
name = 09-P1 Mem Zone 
[sensor10]
name = 10-P2 Mem Zone 
[sensor11]
name = 11-P2 Mem Zone 
[sensor12]
name = 12-HD Max 
[sensor13]
name = 13-Chipset 1 
[sensor14]
name = 14-Chipset1 Zone 
[sensor15]
name = 15-P/S 1 Inlet 
[sensor16]
name = 16-P/S 1 Zone 
[sensor17]
name = 17-P/S 2 Inlet 
[sensor18]
name = 18-P/S 2 Zone 
[sensor19]
name = 19-PCI #1 
[sensor20]
name = 20-PCI #2 
[sensor21]
name = 21-VR P1 
[sensor22]
name = 22-VR P2 
[sensor23]
name = 23-VR P1 Mem
[sensor24]
name = 24-VR P1 Mem
[sensor25]
name = 25-VR P2 Mem
[sensor26]
name = 26-VR P2 Mem
[sensor27]
name = 27-VR P1Mem Zone 
[sensor28]
name = 28-VR P1Mem Zone 
[sensor29]
name = 29-VR P2Mem Zone 
[sensor30]
name = 30-VR P2Mem Zone 
[sensor31]
name = 31-HD Controller 
[sensor32]
name = 32-HD Cntlr Zone 
[sensor33]
name = 33-PCI 1 Zone 
[sensor34]
name = 34-PCI 1 Zone 
[sensor35]
name = 35-LOM Card 
[sensor36]
name = 36-PCI 2 Zone 
[sensor37]
name = 37-System Board 
[sensor38]
name = 38-System Board 
[sensor39]
name = 39-Sys Exhaust 
[sensor40]
name = 40-Sys Exhaust 
[sensor41]
name = 41-Sys Exhaust 
[sensor42]
name = 42-SuperCAP Max 

##################################################################################
#
# POWER sensor management
# -----------------------
#
##################################################################################

[POWER]

groups = POWER1

[POWER1]

group = server power

sensortype = power
datatype   = discrete
interval   = 100       ; seconds


# rule = (?:ElementName|
cmd = show -a /system1/powersupply*

ignore = na
pass = ok
minor = na
major = Bad
critical = Failed

health = HealthState:Good,na,Bad,Failed
status = OperationalStatus:Ok,na,Bad,Failed

sensors = powersupply1,powersupply2

[powersupply1]
key  = powersupply1
cmd  = show /system1/powersupply1
name = Power Supply

[powersupply2]
key  = powersupply2
cmd  = show /system1/powersupply2
name = Power Supply
# 380 name = System
