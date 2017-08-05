/**
 * Fetches data from the BuildingOS API and caches it in the db
 *
 * @author Tim Robert-Fitzgerald
 */

#define _XOPEN_SOURCE // for strptime
#define _GNU_SOURCE // for strptime
#define PRIORITY_METER "SELECT id, org_id, url, live_last_updated FROM meters WHERE source = 'buildingos' AND live_last_updated < (UNIX_TIMESTAMP() - 300) AND (for_orb > 0 OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '')) AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') ORDER BY live_last_updated ASC LIMIT 1"
#define LIVE_TARGET_METER "SELECT id, org_id, url, live_last_updated FROM meters WHERE source = 'buildingos' AND ((gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '')) AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') ORDER BY live_last_updated ASC LIMIT 1"
#define QH_TARGET_METER "SELECT id, org_id, url, quarterhour_last_updated FROM meters WHERE source = 'buildingos' AND ((gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '')) AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') ORDER BY quarterhour_last_updated ASC LIMIT 1"
#define HOUR_TARGET_METER "SELECT id, org_id, url, hour_last_updated FROM meters WHERE source = 'buildingos' AND ((gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '')) AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') ORDER BY hour_last_updated ASC LIMIT 1"
#define MONTH_TARGET_METER "SELECT id, org_id, url, month_last_updated FROM meters WHERE source = 'buildingos' AND ((gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '')) AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') ORDER BY month_last_updated ASC LIMIT 1"
#define UPDATE_LIVE_TIMESTAMP "UPDATE meters SET live_last_updated = %d WHERE id = %d"
#define UPDATE_QH_TIMESTAMP "UPDATE meters SET quarterhour_last_updated = %d WHERE id = %d"
#define UPDATE_HOUR_TIMESTAMP "UPDATE meters SET hour_last_updated = %d WHERE id = %d"
#define UPDATE_MONTH_TIMESTAMP "UPDATE meters SET month_last_updated = %d WHERE id = %d"
#define TOKEN_URL "https://api.buildingos.com/o/token/" // where to get the token from
#define ISO8601_FORMAT "%Y-%m-%dT%H:%M:%S%z"
#define ISO8601_FORMAT_EST "%Y-%m-%dT%H:%M:%S-04:00"
#define SMALL_CONTAINER 255 // small fixed-size container for arrays
#define LIVE_DATA_LIFESPAN 7200 // live data is stored for 2 hours i.e. 7200s
#define QH_DATA_LIFESPAN 1209600 // 2 weeks
#define HOUR_DATA_LIFESPAN 5184000 // 2 months
#define MONTH_DATA_LIFESPAN 63113904 // 2 years
#define UPDATE_CURRENT 1 // update the meters.current column with the current reading?
#define READONLY_MODE 0 // if on (i.e. 1) the daemon will not make queries that update/insert/delete data by short circuiting if stmts

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <mysql.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <sys/stat.h>
#include <curl/curl.h> // install with `apt-get install libcurl4-openssl-dev`
#include <curl/easy.h>
#include "./lib/cJSON/cJSON.h"
#include "db.h"

static pid_t buildingosd_pid;
void cleanup(MYSQL *conn); // do this so error() knows about cleanup()

/**
 * Utility function copied from https://stackoverflow.com/a/779960/2624391
 */
char *str_replace(char *orig, char *rep, char *with) {
	char *result; // the return string
	char *ins;    // the next insert point
	char *tmp;    // varies
	int len_rep;  // length of rep (the string to remove)
	int len_with; // length of with (the string to replace rep with)
	int len_front; // distance between rep and end of last rep
	int count;    // number of replacements

	// sanity checks and initialization
	if (!orig || !rep) {
	    return NULL;
	}
	len_rep = strlen(rep);
	if (len_rep == 0) {
	    return NULL; // empty rep causes infinite loop during count
	}
	if (!with) {
	    with = "";
	}
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count) {
	    ins = tmp + len_rep;
	}

	tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

	if (!result) {
	    return NULL;
	}

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--) {
	    ins = strstr(orig, rep);
	    len_front = ins - orig;
	    tmp = strncpy(tmp, orig, len_front) + len_front;
	    tmp = strcpy(tmp, with) + len_with;
	    orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}

/**
 * Signal handler
 * @param signo [description]
 */
static void catch_signal(int signo) {
	system("/var/www/html/oberlin/daemons/buildingosd -d"); // lol
	syslog(LOG_ERR, "Caught pipe #%d; exiting", signo);
}

/**
 * daemonizes a process by disconnecting it from the shell it was started in
 * mostly follows the steps described by `man 7 daemon`
 * See https://stackoverflow.com/questions/17954432/creating-a-daemon-in-linux for code
 */
static void daemonize() {
	pid_t pid;
	/* Fork off the parent process */
	pid = fork();
	/* An error occurred */
	if (pid < 0)
		exit(EXIT_FAILURE);
	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);
	/* On success: The child process becomes session leader */
	if (setsid() < 0)
		exit(EXIT_FAILURE);
	/* Catch, ignore and handle signals */
	//TODO: Implement a working signal handler */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if (pid < 0)
		exit(EXIT_FAILURE);
	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);
	/* Set new file permissions */
	umask(0);
	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	chdir("/");
	/* Close all open file descriptors */
	int x;
	for (x = sysconf(_SC_OPEN_MAX); x>=0; x--) {
		close(x);
	}
}


// Stores page downloaded by http_request()
struct MemoryStruct {
	char *memory;
	size_t size;
};

/**
 * Helper for http_request()
 */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;
	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		fprintf(stderr, "not enough memory (realloc returned NULL)\n");
		return 0;
	}
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	return realsize;
}

/**
 * See https://curl.haxx.se/libcurl/c/postinmemory.html
 * @param url           http://www.example.org/
 * @param post          e.g. Field=1&Field=2&Field=3
 * @param custom_header 1 for a custom header, 0 for default
 * @param method        1 if POST, 0 if GET
 */
struct MemoryStruct http_request(char *url, char *post, int custom_header, int method, char *api_token) {
	char header[50];
	if (custom_header) {
		sprintf(header, "Authorization: Bearer %s", api_token);
	}
	CURL *curl;
	CURLcode res;
	struct MemoryStruct chunk;
	chunk.memory = malloc(1);  /* will be grown as needed by realloc above */ 
	chunk.size = 0;    /* no data at this point */ 
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if (curl) {
		if (custom_header) {
			struct curl_slist *chunk = NULL; // https://curl.haxx.se/libcurl/c/httpcustomheader.html
			/* Add a custom header */ 
			chunk = curl_slist_append(chunk, header);
			res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
		}
		if (method == 1) {
			curl_easy_setopt(curl, CURLOPT_URL, url);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post); // if we don't provide POSTFIELDSIZE, libcurl will strlen() by itself
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(post)); // Perform the request, res will get the return code
		} else {
			char full_url[SMALL_CONTAINER];
			strcpy(full_url, url);
			strcat(full_url, "?");
			strcat(full_url, post);
			curl_easy_setopt(curl, CURLOPT_URL, full_url);
			curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
		}
		// curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
		/* send all data to this function  */ 
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		/* we pass our 'chunk' struct to the callback function */ 
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
		/* some servers don't like requests that are made without a user-agent
			 field, so we provide one */ 
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
		res = curl_easy_perform(curl);
		/* Check for errors */ 
		if (res != CURLE_OK) {
			syslog(LOG_ERR, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
			exit(1);
		}
		curl_easy_cleanup(curl);
		curl_global_cleanup();
	}
	return chunk;//chunk.memory;
	// free(chunk.memory);
}

/**
 * Handle errors
 */
void error(const char *msg, MYSQL *conn) {
	syslog(LOG_ERR, "%s", msg);
	cleanup(conn);
}

/**
 * Execute before program termination
 */
void cleanup(MYSQL *conn) {
	char query[SMALL_CONTAINER];
	sprintf(query, "DELETE FROM daemons WHERE pid = %d", buildingosd_pid);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) {
		syslog(LOG_ERR, "%s", mysql_error(conn));
	}
	closelog();
	mysql_close(conn);
	exit(1); // this might just kill the child, but since the mysql conn is closed, daemon will die
}

/**
 * Fetches a single record, terminating the program if there are no results
 */
MYSQL_ROW fetch_row(MYSQL *conn, char *query) {
	MYSQL_RES *res;
	MYSQL_ROW row;
	if (mysql_query(conn, query)) {
		error(mysql_error(conn), conn);
	}
	// res = mysql_use_result(conn); // this doesnt work?
	res = mysql_store_result(conn);
	row = mysql_fetch_row(res);
	mysql_free_result(res);
	if (row == NULL) {
		syslog(LOG_ERR, "QUERY '%s' RETURNED 0 ROWS\n", query);
		cleanup(conn);
	}
	return row;
}

/**
 * Sets the API token, fetching a new one if necessary
 * @param conn
 * @param org_id to get API credentials for
 */
char *set_api_token(MYSQL *conn, char *org_id) {
	char query[SMALL_CONTAINER];
	MYSQL_ROW row;
	sprintf(query, "SELECT api_id FROM orgs WHERE id = %s", org_id);
	int api_id = atoi(fetch_row(conn, query)[0]);
	sprintf(query, "SELECT token, token_updated FROM api WHERE id = %d", api_id);
	row = fetch_row(conn, query);
	int update_token_at = atoi(row[1]) + 3595;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int time = tv.tv_sec;
	if (update_token_at > time) { // token still not expired
		return row[0];
	} else { // amortized cost; need to get new API token
		sprintf(query, "SELECT client_id, client_secret, username, password FROM api WHERE id = '%d'", api_id);
		row = fetch_row(conn, query);
		char post_data[SMALL_CONTAINER];
		sprintf(post_data, "client_id=%s&client_secret=%s&username=%s&password=%s&grant_type=password", row[0], row[1], row[2], row[3]);
		struct MemoryStruct response = http_request(TOKEN_URL, post_data, 0, 1, "");
		cJSON *root = cJSON_Parse(response.memory);
		cJSON *access_token = cJSON_GetObjectItem(root, "access_token");
		char *api_token = access_token->valuestring;
		sprintf(query, "UPDATE api SET token = '%s', token_updated = %d WHERE id = %d", api_token, time, api_id);
		if (mysql_query(conn, query)) { // do this even if READONLY_MODE is on bc it cant hurt to update the api token
			error(mysql_error(conn), conn);
		}
		free(response.memory);
		cJSON_free(root);
		return api_token;
	}
}

/**
 * Updates a meter
 * this function has way too many parameters, but it's better than globals
 * @param conn
 * @param meter_id
 * @param meter_url
 * @param resolution
 * @param start_time    the earlier date
 * @param end_time      the later date
 */
void update_meter(MYSQL *conn, int meter_id, char *meter_url, char *api_token, char *resolution, time_t start_time, time_t end_time, int verbose) {
	struct tm *ts;
	char iso8601_end_time[30];
	char iso8601_start_time[30];
	char query[SMALL_CONTAINER];
	ts = localtime(&end_time);
	strftime(iso8601_end_time, sizeof(iso8601_end_time), ISO8601_FORMAT_EST, ts);
	// printf("%d %s\n", end_time, iso8601_end_time);
	ts = localtime(&start_time);
	strftime(iso8601_start_time, sizeof(iso8601_start_time), ISO8601_FORMAT_EST, ts);
	// printf("%d %s\n", (int) start_time, iso8601_start_time);
	// Make call to the API for meter data
	char post_data[SMALL_CONTAINER];
	sprintf(post_data, "resolution=%s&start=%s&end=%s", resolution, str_replace(iso8601_start_time, ":", "%3A"), str_replace(iso8601_end_time, ":", "%3A"));
	struct MemoryStruct response = http_request(meter_url, post_data, 1, 0, api_token);
	cJSON *root = cJSON_Parse(response.memory);
	if (!cJSON_HasObjectItem(root, "data")) {
		error(response.memory, conn);
	}
	cJSON *data = cJSON_GetObjectItem(root, "data");
	// insert new data
	// char sql_data[SMALL_CONTAINER];
	FILE *buffer = fopen("/root/meter_data.csv", "a");
	if (buffer == NULL) {
	    error("Error opening meter_data buffer", conn);
	}
	// int insert_sql_size = SMALL_CONTAINER;
	// char *insert_sql = malloc(sizeof(char) * SMALL_CONTAINER);
	// if (insert_sql == NULL) {
	// 	error("malloc returned NULL", conn);
	// }
	// strcpy(insert_sql, "INSERT INTO meter_data (meter_id, value, recorded, resolution) VALUES ");
	int data_size = cJSON_GetArraySize(data);
	double last_non_null = -9999.0; // error value
	for (int i = 0; i < data_size; i++) {
		cJSON *data_point = cJSON_GetArrayItem(data, i);
		cJSON *data_point_val = cJSON_GetObjectItem(data_point, "value");
		cJSON *data_point_time = cJSON_GetObjectItem(data_point, "localtime");
		char val[10];
		if (data_point_val->type == 4) {
			// val[0] = 'N'; val[1] = 'U'; val[2] = 'L'; val[3] = 'L'; val[4] = '\0'; // srsly?
			val[0] = '\\'; val[1] = 'N'; val[2] = '\0'; // https://stackoverflow.com/a/2675493
		} else {
			last_non_null = data_point_val->valuedouble;
			sprintf(val, "%.3f", last_non_null);
		}
		// https://stackoverflow.com/a/1002631/2624391
		struct tm ltm = {0};
		time_t epoch = 0;
		if (strptime(data_point_time->valuestring, ISO8601_FORMAT, &ltm) != NULL) {
			epoch = mktime(&ltm) - 3600;
		} else {
			error("Unable to parse date", conn);
		}
		fprintf(buffer, "%d,%s,%d,\"%s\"\n", meter_id, val, (int) epoch, resolution);
		// sprintf(sql_data, "(%d, %s, %d, '%s')", meter_id, val, (int) epoch, resolution);
		// strcat(insert_sql, sql_data);
		// if ((i + 1) == data_size) {
		// 	strcat(insert_sql, ";");
		// } else { // more data to process
		// 	strcat(insert_sql, ", ");
		// 	insert_sql_size += 70;
		// 	insert_sql = realloc(insert_sql, insert_sql_size);
		// }
		if (verbose) {
			printf("\"%d\", \"%s\", \"%d\", \"%s\"\n", meter_id, val, (int) epoch, resolution);
		}
	}
	fclose(buffer);
	// if (READONLY_MODE == 0 && mysql_query(conn, insert_sql)) {
	// 	error(mysql_error(conn), conn);
	// }
	// free(insert_sql);
	free(response.memory);
	#if UPDATE_CURRENT == 1
	if (last_non_null != -9999.0 && strcmp(resolution, "live") == 0) {
		query[0] = '\0';
		sprintf(query, "UPDATE meters SET current = %.3f WHERE id = %d", last_non_null, meter_id);
		if (READONLY_MODE == 0 && mysql_query(conn, query)) {
			error(mysql_error(conn), conn);
		}
	}
	#endif
}

// void launch_with_default_options() {
// 	system("/var/www/html/oberlin/daemons/buildingosd -d");
// }

int main(int argc, char *argv[]) {
	// atexit(launch_with_default_options); // causes a crash
	// data fetched spans from start_time to end_time
	time_t end_time;
	time_t start_time;
	int opt;
	int data_lifespan;
	int move_back_amount;
	int secs_in_res;
	char *target_meter;
	char *update_timestamp_col;
	char tmp[SMALL_CONTAINER];
	// If the -o flag is set, the program will update a single meter instead of looping
	int o_flag = 0;
	// if the -d flag is set, the program will become a true daemon, disconnecting itself from the shell it was started in
	int d_flag = 0;
	// -v flag prints debugging information
	int v_flag = 0;
	// if the -r flag is set with one of "live", "quarterhour", "hour", or "month" the program will fetch the specified resolution
	// when fetching "live" data, the program will always fetch the newest data i.e. data spanning from the last recorded date in our db to now
	// when fetching other resolutions, the program will be checking that all of that data is there, because normally it is calculated based of min data by crons. if all the data is not there, it will fetch it
	char *r_flag = NULL;
	while ((opt = getopt (argc, argv, "r:odv")) != -1) {
		switch (opt) {
			case 'r': // "resolution"
				r_flag = optarg;
				break;
			case 'o': // run "once"
				o_flag = 1;
				break;
			case 'd': // "daemon"
				d_flag = 1;
				break;
			case 'v': // "verbose"
				v_flag = 1;
				break;
		}
	}
	if (d_flag) {
		if (v_flag) {
			printf("Can't use -d and -v at same time; ignoring -v flag\n");
			v_flag = 0;
		}
		daemonize();
	}
	if (r_flag == NULL) {
		r_flag = "live";
	}
	// interpret command line input
	int live_res = 0;
	if (strcmp(r_flag, "live") == 0) {
		target_meter = LIVE_TARGET_METER;
		update_timestamp_col = UPDATE_LIVE_TIMESTAMP;
		data_lifespan = LIVE_DATA_LIFESPAN;
		move_back_amount = 180; // meant to move meters back in the queue of what's being updated by update_meter() so they don't hold up everything if update_meter() keeps failing for some reason. note that if update_meter() does finish, it pushes the meter to the end of the queue by updating the last_updated_col to the current time otherwise the last_updated_col remains the current time minus this amount.
		secs_in_res = 60;
		live_res = 1;
	} else if (strcmp(r_flag, "quarterhour") == 0) {
		target_meter = QH_TARGET_METER;
		update_timestamp_col = UPDATE_QH_TIMESTAMP;
		data_lifespan = QH_DATA_LIFESPAN;
		move_back_amount = 480;
		secs_in_res = 900;
	} else if (strcmp(r_flag, "hour") == 0) {
		target_meter = HOUR_TARGET_METER;
		update_timestamp_col = UPDATE_HOUR_TIMESTAMP;
		data_lifespan = HOUR_DATA_LIFESPAN;
		move_back_amount = 900;
		secs_in_res = 3600;
	} else if (strcmp(r_flag, "month") == 0) {
		target_meter = MONTH_TARGET_METER;
		update_timestamp_col = UPDATE_MONTH_TIMESTAMP;
		data_lifespan = MONTH_DATA_LIFESPAN;
		move_back_amount = 86400;
		secs_in_res = 2592000;
	} else {
		printf("Please provide a proper resolution via the -r flag\n");
		return 1;
	}
	buildingosd_pid = getpid(); // save this in a global so the children know
	MYSQL *conn;
	conn = mysql_init(NULL);
	// Connect to database
	if (!mysql_real_connect(conn, DB_SERVER,
	DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
		error(mysql_error(conn), conn);
	}
	// Insert record of daemon
	char query[SMALL_CONTAINER];
	sprintf(query, "INSERT INTO daemons (pid, enabled, target_res) VALUES (%d, %d, '%s')", buildingosd_pid, 1, r_flag);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) { // short circuit
		error(mysql_error(conn), conn);
	}
	openlog("buildingosd", LOG_PID, LOG_DAEMON);
	signal(SIGPIPE, catch_signal);
	sprintf(query, "SELECT enabled FROM daemons WHERE pid = %d", buildingosd_pid); // dont modify query variable again
	while (1) {
		MYSQL_RES *res;
		MYSQL_ROW row;
		MYSQL_ROW meter;
		time_t now = time(NULL);
		if (READONLY_MODE == 0) {
			// if the daemon is 'enabled' in the db
			if (mysql_query(conn, query)) { // this line triggers a SIGPIPE?
				error(mysql_error(conn), conn);
			}
			res = mysql_use_result(conn);
			row = mysql_fetch_row(res);
			mysql_free_result(res);
			if (row == NULL) { // record of daemon does not exist
				error("I should not exist", conn);
			} else if (row[0][0] == '0') { //(strcmp(row[0], "1") != 0) {
				// if enabled column turned off, exit
				if (d_flag) {
					error("Enabled column switched off", conn);
				} else {
					puts("Enabled column switched off");
					cleanup(conn);
				}
			}
		}
		if (live_res) { // make sure the priority meters (i.e. the orbs) are always up to date
			if (mysql_query(conn, PRIORITY_METER)) {
				error(mysql_error(conn), conn);
			}
			res = mysql_store_result(conn);
			meter = mysql_fetch_row(res);
			mysql_free_result(res);
		}
		if (live_res == 0 || meter == NULL) { // if the orbs are up to date or we're collecting non-minute resolution data
			meter = fetch_row(conn, target_meter);
		}
		char meter_url[SMALL_CONTAINER];
		meter_url[0] = '\0';
		int meter_id = atoi(meter[0]);
		char *org_id = meter[1];
		strcat(meter_url, meter[2]);
		strcat(meter_url, "/data");
		int last_updated = atoi(meter[3]);
		sprintf(tmp, "UPDATE daemons SET updating_meter = %d WHERE pid = %d", meter_id, buildingosd_pid);
		if (READONLY_MODE == 0) {
			if (mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
		}
		if (last_updated > (now - secs_in_res)) { // if the least up to date meter was last updated secs_in_res seconds ago
			int sleep_time = (int) (secs_in_res / 10);
			now += sleep_time;
			if (live_res) {
				sleep(sleep_time);
			} else {
				return EXIT_SUCCESS;
			}
		}
		// Set start/end time
		if (live_res) {
			// if live res, fetch data spanning from the latest point recorded in the db to now
			end_time = now;
			sprintf(tmp, "SELECT recorded FROM meter_data WHERE meter_id = %d AND resolution = '%s' ORDER BY recorded DESC LIMIT 1", meter_id, r_flag);
			if (mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
			res = mysql_store_result(conn);
			row = mysql_fetch_row(res);
			mysql_free_result(res);
			if (row == NULL) { // no data exists for this meter
				start_time = end_time - (time_t) data_lifespan;
			} else {
				start_time = (time_t) atoi(row[0]);
			}
		} else {
			// if other res, only make sure data goes back as far as it's supposed to
			// i.e. fetch data spanning from data_lifespan to the earliest point recorded in the db
			start_time = now - (time_t) data_lifespan;
			sprintf(tmp, "SELECT recorded FROM meter_data WHERE meter_id = %d AND resolution = '%s' ORDER BY recorded ASC LIMIT 1", meter_id, r_flag);
			if (mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
			res = mysql_store_result(conn);
			row = mysql_fetch_row(res);
			mysql_free_result(res);
			if (row == NULL) { // no data exists for this meter
				end_time = now;
			} else {
				end_time = (time_t) atoi(row[0]);
				if (end_time < ((now - data_lifespan) + secs_in_res)) { // if the end time goes as far back as we store data for, mark meter as updated and continue
					sprintf(tmp, update_timestamp_col, (int) now, meter_id);
					if (READONLY_MODE == 0 && mysql_query(conn, tmp)) {
						error(mysql_error(conn), conn);
					}
					continue;
				}
			}
		}
		pid_t childpid = fork();
		if (childpid == -1) {
			error("Failed to fork", conn);
		} 
		else if (childpid > 0) {
			int status;
			waitpid(childpid, &status, 0);
		} else { // we are the child
			signal(SIGPIPE, catch_signal);
			sprintf(tmp, update_timestamp_col, (int) now - move_back_amount, meter_id);
			if (READONLY_MODE == 0 && mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
			update_meter(conn, meter_id, meter_url, set_api_token(conn, org_id), r_flag, start_time, end_time, v_flag);
			sprintf(tmp, update_timestamp_col, (int) now, meter_id);
			if (READONLY_MODE == 0 && mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
			if (d_flag == 0) {
				printf("Updated meter %d (fetched data from %d to %d)\n", meter_id, (int) start_time, (int) end_time);
			}
			// else {
			// 	syslog(LOG_INFO, "Updated meter %d (fetched %s data from %d to %d)\n", meter_id, r_flag, (int) start_time, (int) end_time);
			// }
			exit(1);
		}
		if (o_flag == 1) {
			break;
		}
	}
	cleanup(conn);
	mysql_close(conn);
	return EXIT_SUCCESS;
}