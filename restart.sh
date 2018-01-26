#!/bin/bash

source /var/repos/daemons/db.sh
pop=2 # keep this number of buildingosd instances running
sql=""
while read -r line
do # iterate over result of pidof buildingosd
	for pid in $(echo $line | tr " " "\n")
	do
		# echo $pid
		(( pop-- ))
		if (( pop < 0 )); then # make sure there aren't too many daemons running
			kill $pid
			mysql -u $user -"p$pass" -"h$server" $name -BNse "DELETE FROM daemons WHERE pid = $pid" 2>/dev/null
		else # this pid is allowed to exist
			sql="$sql$pid, "
		fi
	done
done <<< `pidof buildingosd`
if [ ! -z "$sql" ]; then # if not empty string, delete the daemons still in the db but not running
	sql=${sql:0:-2} # cut last two chars ", "
	mysql -u $user -"p$pass" "-h$server" $name -BNse "DELETE FROM daemons WHERE pid NOT IN ($sql)" 2>/dev/null
	# echo "DELETE FROM daemons WHERE pid NOT IN ($sql)"
fi
for ((i=0; i<pop; i++)); do # make sure there are enough daemons running
	`/var/repos/daemons/buildingosd -d`
done
