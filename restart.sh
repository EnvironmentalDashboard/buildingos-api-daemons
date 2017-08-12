#!/bin/bash

source db.sh
pop=2
while read -r line
do
	for pid in $(echo $line | tr " " "\n")
	do
		if ps -p $pid > /dev/null; then
			(( pop-- ))
			# echo "$pid exists"
		else # process is not running, but is in db
			mysql -u $user -"p$pass" -"h$server" $name -BNse "DELETE FROM daemons WHERE pid = $pid" 2>/dev/null
			# echo "$pid doesnt exist"
		fi
		if (( pop == 0 )); then
			exit 0
		fi
	done
done <<< `mysql -u $user -"p$pass" -"h$server" $name -BNse "SELECT pid FROM daemons WHERE target_res = 'live'" 2>/dev/null`

for ((i=0; i<pop; i++)); do
	`/var/www/html/oberlin/daemons/buildingosd -d`
done
