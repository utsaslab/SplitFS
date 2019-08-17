#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters; Please provide sleep time as the parameter;"
    exit 1
fi
sleep_time=$1
#sleep_time=300

echo "pause" > ~/projects/leveldb/scripts/pause

while true; 
do
	pause=`cat ~/projects/leveldb/scripts/pause`
	if [ "$pause" == "pause" ]
	then	
		echo `date` ' Sleeping for '$sleep_time' seconds . . .'
		sleep $sleep_time
		echo "" > ~/projects/leveldb/scripts/pause
	else
		break;
	fi
done

echo 'Pause released. Exiting..'
