###################################################################################
#
# This file contains the sensor profile for the following board
#
#    "Quanta Computer" with "Quanta(TM) Embedded Lights Out Manager ; v3.29"
# 
###################################################################################
#
# Please refer to sensor_integration_profile.README for a detailed
# explaination of the format and heirarchy of this file and how to
# develop a file like this for sensor integration for new servers.
#
####################################################################################

[SERVER]

targets  = Targets:SP:SYS
name_cmd = show /SYS
bmc_cmd  = show /SP
bmv_ver  = v3.29
name     = Quanta Computer
bmc      = Quanta(TM) Embedded Lights Out Manager
info     = show /SYS
dump_cmd = show /SP           ; there is no dump, this just prints the bmc version

group_types = COOLING,POWER,TEMPERATURE,VOLTAGE

[CMDS]
reset = reset /SYS
poweron = start /SYS
poweroff = stop /SYS
powerquery = show /SYS

[FIT]
hostname = none
sensor = none
severity = none

##################################################################################
#
# POWER sensor management
# -----------------------------
#
##################################################################################

[POWER]
groups = POWER1

[POWER1]
group = server power

sensortype = power
datatype   = discrete      
interval   = 100             ; seconds

cmd = show /SYS/powerSupply

sensors = PSU12,PSU1,PSU2

[PSU12]
name = PSU Redundancy

[PSU1]
name = PSU1 Status

[PSU2]
name = PSU2 Status

##################################################################################
#
# COOLING sensor management
# -----------------------------
#
##################################################################################

[COOLING]
groups = FANS1,FANS2

##############################
# COOLING:FANS1 Grouping
##############################

[FANS1]
group = server fans

sensortype = fan
datatype   = discrete      
interval   = 120             ; seconds

cmd = show /SYS/fan

# TODO: the sensor reading rules
ignore = na
pass = ok
minor = na
major = nonCritical
critical = critical

delimitor = =

sensors = FANS1_1,FANS1_2,FANS1_3,FANS1_4,FANS1_5,FANS1_6,FANS1_7,FANS1_8,FANS1_9,FANS1_10,FANS1_11,FANS1_12

[FANS1_1]
name = Fan_SYS0_1

[FANS1_2]
name = Fan_SYS0_2

[FANS1_3]
name = Fan_SYS1_1

[FANS1_4]
name = Fan_SYS1_2

[FANS1_5]
name = Fan_SYS2_1

[FANS1_6]
name = Fan_SYS2_2

[FANS1_7]
name = Fan_SYS3_1

[FANS1_8]
name = Fan_SYS3_2

[FANS1_9]
name = Fan_SYS4_1

[FANS1_10]
name = Fan_SYS4_2

[FANS1_11]
name = Fan_SYS5_1

[FANS1_12]
name = Fan_SYS5_2


##############################
# COOLING:FANS2 Grouping
##############################
[FANS2]
# The name of the group that will show up in the GUI
group = power supply fans

# sensor attributes for this group
sensortype = fan
datatype   = discrete      
interval   = 120             ; seconds

# the commands that will read the sensors in this group
cmd = show /SYS/fan

# TODO: the sensor reading rules
ignore = na
pass = ok
minor = na
major = nonCritical
critical = critical

# Status output delimiter
delimitor = =

# list of abstract labels for the sensors in this group
sensors = FANS2_1,FANS2_2

# the individual sensors in this group
[FANS2_1]
name = Fan_PSU1

[FANS2_2]
name = Fan_PSU2

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

sensortype = temperature
datatype   = discrete      
interval   = 34             ; seconds
unit       = Celsius

cmd = show /SYS/temperature

delimitor = =

# TODO: the sensor reading rules
ignore = na
pass = ok
minor = na
major = nonCritical
critical = critical

sensors = TEMP3,TEMP4,TEMP5,TEMP6,TEMP7,TEMP8,TEMP9,TEMP10,TEMP11,TEMP12,TEMP13,TEMP14,TEMP15,TEMP16,TEMP17,TEMP18,TEMP19,TEMP20,TEMP21,TEMP23,TEMP24

# the individual sensors in this group
[TEMP1]
name = PCH Thermal Trip
[TEMP2]
name = MB Thermal Trip
[TEMP3]
name = Temp_CPU0
[TEMP4]
name = Temp_CPU1
[TEMP5]
name = Temp_VR_CPU0
[TEMP6]
name = Temp_VR_CPU1
[TEMP7]
name = Temp_DIMM_AB
[TEMP8]
name = Temp_DIMM_CD
[TEMP9]
name = Temp_DIMM_EF
[TEMP10]
name = Temp_DIMM_GH
[TEMP11]
name = Temp_VR_DIMM_AB
[TEMP12]
name = Temp_VR_DIMM_CD
[TEMP13]
name = Temp_VR_DIMM_EF
[TEMP14]
name = Temp_VR_DIMM_GH
[TEMP15]
name = Temp_Ambient_FP
[TEMP16]
name = Temp_PCI_Area
[TEMP17]
name = Temp_PCI_Inlet1
[TEMP18]
name = Temp_PCI_Inlet2
[TEMP19]
name = Temp_PCH
[TEMP20]
name = Temp_Outlet
[TEMP21]
name = Temp_HBA_LSI
[TEMP22]
name = Temp_OCP
[TEMP23]
name = Temp_PSU1
[TEMP24]
name = Temp_PSU2     

##################################################################################
#
# VOLTAGE sensor management
# -----------------------------
#
##################################################################################

[VOLTAGE]
groups = VOLTAGE1

[VOLTAGE1]
group      = server voltage

sensortype = voltage
datatype   = discrete      
interval   = 300             ; seconds

cmd = show /SYS/voltage

# TODO: the sensor reading rules
ignore = na
pass = ok
minor = na
major = nonCritical
critical = critical

sensors = VOLT1,VOLT2,VOLT3,VOLT4,VOLT5,VOLT6,VOLT7,VOLT8,VOLT9,VOLT10,VOLT11,VOLT12,VOLT13,VOLT14

# the individual sensors in this group
[VOLT1]
name = Volt_VR_CPU0
[VOLT2]
name = Volt_VR_CPU1
[VOLT3]
name = Volt_P5V
[VOLT4]
name = Volt_P5V_AUX
[VOLT5]
name = Volt_P3V3
[VOLT6]
name = Volt_P1V05
[VOLT7]
name = Volt_P1V8_AUX
[VOLT8]
name = Volt_P12V
[VOLT9]
name = Volt_P3V3_AUX
[VOLT10]
name = Volt_VR_DIMM_AB
[VOLT11]
name = Volt_VR_DIMM_CD
[VOLT12]
name = Volt_VR_DIMM_EF
[VOLT13]
name = Volt_VR_DIMM_GH
[VOLT14]
name = Volt_P3V_BAT   
