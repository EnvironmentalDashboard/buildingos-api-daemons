#!/bin/bash

source db.sh
pop=2 # keep this number of buildingosd instances running
while read -r line
do
	for pid in $(echo $line | tr " " "\n")
	do
		if ps -p $pid > /dev/null; then # process is running
			(( pop-- ))
		else # process is not running, but is in db
			mysql -u $user -"p$pass" -"h$server" $name -BNse "DELETE FROM daemons WHERE pid = $pid" 2>/dev/null
		fi
		if (( pop == 0 )); then # more daemons exist than there should be
			mysql -u $user -"p$pass" -"h$server" $name -BNse "DELETE FROM daemons WHERE pid < $pid" 2>/dev/null
			exit 0
		fi
	done
done <<< `mysql -u $user -"p$pass" -"h$server" $name -BNse "SELECT pid FROM daemons WHERE target_res = 'live' ORDER BY pid DESC" 2>/dev/null` # order by desc bc those processes were started more recently

for ((i=0; i<pop; i++)); do # keep pop number of daemons running
	`/var/www/html/oberlin/daemons/buildingosd -d`
done
