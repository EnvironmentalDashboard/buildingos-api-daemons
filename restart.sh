#!/bin/bash

source db.sh
pop=2 # keep this number of buildingosd instances running
sql=""
while read -r line
do
	for pid in $(echo $line | tr " " "\n")
	do
		# echo $pid
		(( pop-- ))
		if (( pop < 0 )); then
			kill $pid
			mysql -u $user -"p$pass" -"h$server" $name -BNse "DELETE FROM daemons WHERE pid = $pid" 2>/dev/null
		else # this pid is allowed to exist
			sql="$sql$pid, "
		fi
	done
done <<< `pidof buildingosd`
if [ ! -z "$sql" ]; then
	sql=${sql:0:-2} # cut last two chars ", "
	mysql -u $user -"p$pass" -"h$server" $name -BNse "DELETE FROM daemons WHERE pid NOT IN ($sql)" 2>/dev/null
fi
for ((i=0; i<pop; i++)); do # keep pop number of daemons running
	`/var/www/html/oberlin/daemons/buildingosd -d`
done
