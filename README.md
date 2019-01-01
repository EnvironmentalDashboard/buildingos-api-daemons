# BuildingOS API data collection daemon

[![Build Status](https://travis-ci.org/EnvironmentalDashboard/buildingos-api-daemons.svg?branch=master)](https://travis-ci.org/EnvironmentalDashboard/buildingos-api-daemons)

## Overview
The purpose of the [BuildingOS][1] API daemon (`buildingosd`) is to perpetually download meter data from the [BuildingOS API][2] and cache it in a MySQL database for faster access. By default, `buildingosd` will collect "live" (i.e. 1-2 min) resolution data. `buildingosd` will continuously update the least up-to-date meter, requesting live data that spans from the latest reading to the current time. Other resolutions (quarterhour, hour, and month) are calculated by cron jobs using the live data, so there is no need to collect this data from the API. When a new meter is added, however, `buildingosd` can be run to collect non-live resolutions so there is no need to wait for the data to accumulate. When collecting non-live resolution data, the requested data will span from the begining of the time period that the data is stored for to the earliest reading. `buildingosd` has 4 options that help with debugging and allow the resolution collected to be specified.

### Options
- `-r` (**r**esolution) flag: If set with one of "live", "quarterhour", "hour", or "month" the program will fetch the specified resolution
- `-o` (run **o**nce) flag: Only collect data for one meter (always the least up to date meter)
- `-d` (**d**aemon) flag: If set, the program will disconnect itself from the terminal it was started in, thus becoming a daemon. 
- `-v` (**v**erbose): If set, the program will print what it is doing. As the `-d` option involves disconnecting I/O from the TTY, `-v` and `-d` can not be used together.

### Notes
Before compiling, you will have to define the `db.h` file which contains the definitions for connecting to the MYSQL server. An example `db.h` file would look like this:

```cpp
#define DB_SERVER "localhost"
#define DB_USER "user"
#define DB_PASS "1234"
#define DB_NAME "dbname"
```



[1]: https://lucidconnects.com/solutions
[2]: http://docs.buildingosapi.apiary.io/#