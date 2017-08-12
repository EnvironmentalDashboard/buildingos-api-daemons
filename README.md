# BuildingOS API data collection daemon


## Overview
The purpose of the [BuildingOS](1) API daemon (`buildingosd`) is to perpetually download meter data from the [BuildingOS API](2) and cache it in a MySQL database for faster access. By default, `buildingosd` will collect "live" (i.e. 1-2 min) resolution data. `buildingosd` will continuously update the least up-to-date meter, requesting live data that spans from the latest reading to the current time. Other resolutions (quarterhour, hour, and month) are calculated by cron jobs using the live data, so there is no need to collect this data from the API. When a new meter is added, however, `buildingosd` can be run to collect non-live resolutions so there is no need to wait for the data to accumulate. When collecting non-live resolution data, the requested data will span from the begining of the time period that the data is stored for to the earliest reading. `buildingosd` has 4 options that help with debugging and allow the resolution collected to be specified.

- `-r` (**r**esolution) flag: If set with one of "live", "quarterhour", "hour", or "month" the program will fetch the specified resolution
- `-o` (run **o**nce) flag: Only collect data for one meter (always the least up to date meter)
- `-d` (**d**aemon) flag: If set, the program will disconnect itself from the terminal it was started in, thus becoming a daemon. 
- `-v` (**v**erbose): If set, the program will print what it is doing. As the `-d` option involves disconnecting I/O from the TTY, `-v` and `-d` can not be used together.


---

## Installation
To collect data from the BuildingOS API using `buildingosd`, you must compile the program, have a similar database schema, and install some [cron jobs](#crons).
#### Compiling
Before compiling, you will have to define the `db.h` file which contains the definitions for connecting to the MYSQL server. An example `db.h` file would look like this:

```cpp
#define DB_SERVER "localhost"
#define DB_USER "user"
#define DB_PASS "1234"
#define DB_NAME "dbname"
```
If your compiler can not find `mysql.h` (assuming you have it installed), you may have to [compile with](3) the `-I` flag and define a path for `gcc` to check e.g. `-I/usr/include/mysql`. If you do not have have libcurl installed, you will need to run `apt-get install libcurl4-openssl-dev`. After that, just `make all`.
#### Database schema
`buildingosd` assumes 4 tables exist.

- `daemons` (used to track daemons and sync multiple instances)

    ```sql
    CREATE TABLE `daemons` (
      `pid` int(11) NOT NULL DEFAULT '0',
      `enabled` tinyint(1) NOT NULL DEFAULT '0',
      `target_res` enum('live','quarterhour','hour','month') NOT NULL,
      `updating_meter` int(11) NOT NULL DEFAULT '0' COMMENT 'The ID of the meter this daemon is currently updating',
      `last_updated` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    ) ENGINE=InnoDB DEFAULT CHARSET=latin1;
    ALTER TABLE `daemons`
      ADD PRIMARY KEY (`pid`);
    ```
    Note that if you run multiple instances of `buildingosd` on multiple machines, `pid` can not be used as the primary key as it is possible for a two programs running on two different machines to have the same `pid`.

- `meters` (stores meter meta data such as meter names)

    ```sql
    CREATE TABLE `meters` (
      `id` int(11) NOT NULL,
      `org_id` int(11) NOT NULL DEFAULT '0',
      `bos_uuid` varchar(255) DEFAULT NULL,
      `name` varchar(255) NOT NULL,
      `url` varchar(2000) NOT NULL,
      `current` decimal(12,3) DEFAULT NULL,
      `units` varchar(255) NOT NULL,
      `live_last_updated` int(10) NOT NULL DEFAULT '0',
      `quarterhour_last_updated` int(10) NOT NULL DEFAULT '0',
      `hour_last_updated` int(10) NOT NULL DEFAULT '0',
      `month_last_updated` int(10) NOT NULL DEFAULT '0',
    ) ENGINE=InnoDB DEFAULT CHARSET=latin1;
    ALTER TABLE `meters`
      ADD PRIMARY KEY (`id`),
      ADD UNIQUE KEY `url` (`url`),
      ADD UNIQUE KEY `bos_uuid` (`bos_uuid`);
    ALTER TABLE `meters`
      MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;
    ```

- `meter_data` (you guessed it, stores meter data)

    ```sql
    CREATE TABLE `meter_data` (
      `id` int(11) UNSIGNED NOT NULL,
      `meter_id` bigint(11) NOT NULL,
      `value` decimal(12,3) DEFAULT NULL,
      `recorded` int(10) NOT NULL,
      `resolution` enum('live','quarterhour','hour','day','month','other') NOT NULL
    ) ENGINE=InnoDB DEFAULT CHARSET=latin1;
    ALTER TABLE `meter_data`
      ADD PRIMARY KEY (`id`);
    ALTER TABLE `meter_data`
      MODIFY `id` int(11) UNSIGNED NOT NULL AUTO_INCREMENT;
    ```

- `orgs` ([Organizations](4) are the top level building hierarchy in BuildingOS. They associate API credentials with buildings and meters)

    ```sql
    CREATE TABLE `orgs` (
      `id` int(11) NOT NULL,
      `api_id` int(11) NOT NULL DEFAULT '0',
      `name` varchar(255) NOT NULL DEFAULT '',
      `url` varchar(2000) NOT NULL DEFAULT ''
    ) ENGINE=InnoDB DEFAULT CHARSET=latin1;
    ALTER TABLE `orgs`
      ADD PRIMARY KEY (`id`),
      ADD UNIQUE KEY `url` (`url`);
    ```
- `api` (Contains the API credentials associated with each organization)

    ```sql
    CREATE TABLE `api` (
      `id` int(11) NOT NULL,
      `user_id` int(11) NOT NULL DEFAULT '0',
      `client_id` varchar(255) NOT NULL,
      `client_secret` varchar(255) NOT NULL,
      `username` varchar(255) NOT NULL,
      `password` varchar(255) NOT NULL,
      `token` varchar(255) NOT NULL DEFAULT '',
      `token_updated` int(11) NOT NULL DEFAULT '0'
    ) ENGINE=InnoDB DEFAULT CHARSET=latin1;
    ALTER TABLE `api`
      ADD PRIMARY KEY (`id`);
    ALTER TABLE `api`
      MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;
    ```

Finally, the `*_TARGET_METER` definitions will probably have to be tweaked to correctly select the least up-to-date meter. The idea is that LIVE_TARGET_METER, QU_TARGET_METER, HOUR_TARGET_METER, and MONTH_TARGET_METER will select the meter whose live_last_updated, quarterhour_last_updated, hour_last_updated, or month_last_updated number is smallest.

In reality these tables have more columns than listed (which is why the `*_TARGET_METER` definitions must be tweaked), but for the sake of simplicity only the necessary columns are shown.

#### Crons
Some cron jobs need to be run to maintain the system.

- Keep the daemons alive (TODO: rewrite as bash script)

    ```bash
    * * * * * php /var/www/html/oberlin/daemons/restart.php >/dev/null 2>&1
    ```

- Import data into the database from a CSV every 20 seconds (`INSERT`s are a bottleneck for the CPU, so `buildingosd` will write the data to a CSV file instead of directly `INSERT`ing the data)

    ```bash
    * * * * * mysql -uuser -p1234 -hlocalhost dbname -e "LOAD DATA LOCAL INFILE '/root/meter_data.csv' INTO TABLE meter_data FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\n' (meter_id, value, recorded, resolution);" >/dev/null 2>&1 && rm /root/meter_data.csv
    * * * * * sleep 20 && mysql -uuser -p1234 -hlocalhost dbname -e "LOAD DATA LOCAL INFILE '/root/meter_data.csv' INTO TABLE meter_data FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\n' (meter_id, value, recorded, resolution);" >/dev/null 2>&1 && rm /root/meter_data.csv
    * * * * * sleep 40 && mysql -uuser -p1234 -hlocalhost dbname -e "LOAD DATA LOCAL INFILE '/root/meter_data.csv' INTO TABLE meter_data FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\n' (meter_id, value, recorded, resolution);" >/dev/null 2>&1 && rm /root/meter_data.csv
    ```

- Delete old data

    ```bash
    # live res is normally stored for 2 hours but actually lets keep it a little longer i.e. 24 hours
    */2 * * * * mysql -sN -uuser -p1234 -hlocalhost dbname -e "DELETE FROM meter_data WHERE resolution = 'live' AND recorded < (UNIX_TIMESTAMP() - 86400)" >/dev/null 2>&1
    */15 * * * * mysql -sN -uuser -p1234 -hlocalhost dbname -e "DELETE FROM meter_data WHERE resolution = 'quarterhour' AND recorded < (UNIX_TIMESTAMP() - 1209600)" >/dev/null 2>&1
    0 * * * * mysql -sN -uuser -p1234 -hlocalhost dbname -e "DELETE FROM meter_data WHERE resolution = 'hour' AND recorded < (UNIX_TIMESTAMP() - 5184000)" >/dev/null 2>&1
    # month data takes so little room, no need to delete
    ```

- Calculate non-live resolutions

    ```bash
    */15 * * * * mysql -sN -uuser -p1234 -hlocalhost dbname -e "INSERT INTO meter_data (meter_id, \`value\`, recorded, resolution) SELECT meter_id, AVG(\`value\`), TRUNCATE(UNIX_TIMESTAMP() - 900, -2), 'quarterhour' FROM meter_data WHERE recorded > TRUNCATE((UNIX_TIMESTAMP() - 900), -2) AND resolution = 'live' GROUP BY meter_id" >/dev/null 2>&1
    0 * * * * mysql -sN -uuser -p1234 -hlocalhost dbname -e "INSERT INTO meter_data (meter_id, \`value\`, recorded, resolution) SELECT meter_id, AVG(\`value\`), TRUNCATE(UNIX_TIMESTAMP() - 3600, -2), 'hour' FROM meter_data WHERE recorded > TRUNCATE((UNIX_TIMESTAMP() - 3600), -2) AND resolution = 'live' GROUP BY meter_id" >/dev/null 2>&1
    0 0 1 * * mysql -sN -uuser -p1234 -hlocalhost dbname -e "INSERT INTO meter_data (meter_id, \`value\`, recorded, resolution) SELECT meter_id, AVG(\`value\`), TRUNCATE(UNIX_TIMESTAMP() - 2592000, -2), 'month' FROM meter_data WHERE recorded > TRUNCATE((UNIX_TIMESTAMP() - 2592000), -2) AND resolution != 'live' GROUP BY meter_id" >/dev/null 2>&1
    ```


## Known issues
There are a few memory leaks which will cause an eventual crash. Running `valgrind --leak-check=yes -v ./buildingosd -o` generates the following summary:

```
LEAK SUMMARY:
   definitely lost: 248 bytes in 1 blocks
   indirectly lost: 8,352 bytes in 2 blocks
     possibly lost: 8,424 bytes in 3 blocks
   still reachable: 80,880 bytes in 3 blocks
        suppressed: 0 bytes in 0 blocks
```
Often, it seems that the program will recieve a `SIGPIPE` when executing a MySQL query. This causes the program to termintate, but before it does, it will launch another instance of itself.



[1]: https://lucidconnects.com/solutions
[2]: http://docs.buildingosapi.apiary.io/#
[3]: https://stackoverflow.com/a/14604638
[4]: http://docs.buildingosapi.apiary.io/#reference/organizations
