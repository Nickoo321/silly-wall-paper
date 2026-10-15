#!/bin/bash
# shot.sh NAME CONFIGNAME [DUMP] -- render G:/UE/OilTank/Saved/Shots/NAME.png with Config/CONFIGNAME.json
D=${3:-G:/UE/OilTank/Dumps/standin-mc/dump.json}
powershell.exe -ExecutionPolicy Bypass -File 'G:\UE\OilTank\Tools\run-oiltank.ps1' -Dump "$D" -Shot "G:/UE/OilTank/Saved/Shots/$1.png" -Config "G:/UE/OilTank/Config/$2.json" -TimeoutMinutes 20 2>&1 | grep -E "exit |$1.png 2560|REFUS|waiting|not found" | cut -c1-200
