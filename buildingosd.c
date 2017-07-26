/**
 * Fetches "live" resolution data from the BuildingOS API and caches it in the db
 * Data older than DATA_LIFESPAN are deleted
 *
 * @author Tim Robert-Fitzgerald
 */

#define _XOPEN_SOURCE // for strptime
#define _GNU_SOURCE // for strptime
#define TARGET_RES "live" // resolution of data to fetch
#define TARGET_METER "SELECT id, org_id, url, live_last_updated FROM meters WHERE source = 'buildingos' AND ((gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '')) AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') ORDER BY live_last_updated ASC LIMIT 1"
#define UPDATE_TIMESTAMP_COL "UPDATE meters SET live_last_updated = %d WHERE id = %d"
#define TOKEN_URL "https://api.buildingos.com/o/token/" // where to get the token from
#define ISO8601_FORMAT "%Y-%m-%dT%H:%M:%S-04:00" // EST is -4:00
#define SMALL_CONTAINER 255 // small fixed-size container for arrays
#define MOVE_BACK_AMOUNT 180 // meant to move meters back in the queue of what's being updated by update_meter() so they don't hold up everything if update_meter() keeps failing for some reason. note that if update_meter() does finish, it pushes the meter to the end of the queue by updating the last_updated_col to the current time otherwise the last_updated_col remains the current time minus this amount
#define DATA_LIFESPAN 7200 // live data is stored for 2 hours i.e. 7200s
#define UPDATE_CURRENT 1 // update the meters.current column with the current reading?
#define READONLY_MODE 0 // if on (i.e. 1) the daemon will not make queries that update/insert/delete data by short circuiting if stmts
#define SKIP_API_TOKEN 0 // set the api_token to an empty string instead of querying the api. also makes http_request() ignore ssl certs so we can fetch from our own server (for testing, set to 0 in production)

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

static char *api_token;
static pid_t buildingosd_pid;

/**
 * daemonizes a process by disconnecting it from the shell it was started in
 * See https://stackoverflow.com/questions/17954432/creating-a-daemon-in-linux
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
	/* Open the log file */
	openlog("buildingosd", LOG_PID, LOG_DAEMON);
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
struct MemoryStruct http_request(char *url, char *post, int custom_header, int method) {
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
		#if SKIP_API_TOKEN == 1
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html
		#endif
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

void cleanup(MYSQL *conn); // do this so error() knows about cleanup()
/**
 * Handle errors
 */
void error(const char *msg, MYSQL *conn) {
	syslog(LOG_ERR, msg);
	cleanup(conn);
}

/**
 * Execute before program termination
 */
void cleanup(MYSQL *conn) {
	syslog(LOG_NOTICE, "killing daemon %d", buildingosd_pid);
	char query[SMALL_CONTAINER];
	sprintf(query, "DELETE FROM daemons WHERE pid = %d", buildingosd_pid);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) {
		syslog(LOG_ERR, mysql_error(conn));
	}
	closelog();
	mysql_close(conn);
	exit(1);
}

/**
 * Fetches a single record
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
		fprintf(stderr, "QUERY '%s' RETURNED 0 ROWS\n", query);
		exit(1);
	}
	return row;
}

/**
 * Sets the API token, fetching a new one if necessary
 * @param conn
 * @param org_id to get API credentials for
 */
void set_api_token(MYSQL *conn, char *org_id) {
	#if SKIP_API_TOKEN == 0
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
		api_token = (char*) row[0];
	} else { // amortized cost; need to get new API token
		sprintf(query, "SELECT client_id, client_secret, username, password FROM api WHERE id = '%d'", api_id);
		row = fetch_row(conn, query);
		char post_data[SMALL_CONTAINER];
		sprintf(post_data, "client_id=%s&client_secret=%s&username=%s&password=%s&grant_type=password", row[0], row[1], row[2], row[3]);
		struct MemoryStruct response = http_request(TOKEN_URL, post_data, 0, 1);
		cJSON *root = cJSON_Parse(response.memory);
		cJSON *access_token = cJSON_GetObjectItem(root, "access_token");
		api_token = (char*) access_token->valuestring;
		sprintf(query, "UPDATE api SET token = '%s', token_updated = %d WHERE id = %d", api_token, time, api_id);
		if (mysql_query(conn, query)) { // do this even if READONLY_MODE is on bc it cant hurt to update the api token
			error(mysql_error(conn), conn);
		}
		free(response.memory);
		cJSON_free(root);
	}
	#endif
	#if SKIP_API_TOKEN == 1 // I use this when testing with the fake meter #0
	api_token[0] = '\0';
	#endif
}

/**
 * Updates a meter
 * @param conn       [description]
 * @param meter_id   [description]
 * @param meter_url  [description]
 */
void update_meter(MYSQL *conn, int meter_id, char *meter_url) {
	time_t end_time; // end date
	time_t last_recording; // start date
	struct tm *ts;
	char iso8601_time[30];
	char iso8601_last_recording[30];
	end_time = time(NULL);
	ts = localtime(&end_time);
	strftime(iso8601_time, sizeof(iso8601_time), ISO8601_FORMAT, ts);
	// printf("%d %s\n", end_time, iso8601_time);
	// Move the meter back in the queue to be tried again soon in case this function does not complete and the last_updated_col is not updated to the current time
	char query[SMALL_CONTAINER];
	sprintf(query, UPDATE_TIMESTAMP_COL, (int) end_time - MOVE_BACK_AMOUNT, meter_id);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) {
		error(mysql_error(conn), conn);
	}
	// Get the most recent recording. Data fetched from the API will start at last_recording and end at time
	MYSQL_RES *res;
	MYSQL_ROW row;
	sprintf(query, "SELECT recorded FROM meter_data WHERE meter_id = %d AND resolution = '%s' AND value IS NOT NULL ORDER BY recorded DESC LIMIT 1", meter_id, TARGET_RES);
	if (mysql_query(conn, query)) {
		error(mysql_error(conn), conn);
	}
	res = mysql_store_result(conn);
	row = mysql_fetch_row(res);
	mysql_free_result(res);
	if (row == NULL) { // no data exists for this meter
		last_recording = end_time - (time_t) DATA_LIFESPAN;
	} else {
		last_recording = (time_t) atoi(row[0]);
	}
	ts = localtime(&last_recording);
	strftime(iso8601_last_recording, sizeof(iso8601_last_recording), ISO8601_FORMAT, ts);
	// printf("%d %s\n", (int) last_recording, iso8601_last_recording);
	// Make call to the API for meter data
	char post_data[SMALL_CONTAINER];
	char sql_data[SMALL_CONTAINER];
	sprintf(post_data, "resolution=%s&start=%s&end=%s", TARGET_RES, iso8601_last_recording, iso8601_time);
	struct MemoryStruct response = http_request(meter_url, post_data, 1, 0);
	cJSON *root = cJSON_Parse(response.memory);
	cJSON *data = cJSON_GetObjectItem(root, "data");
	// delete old data older than DATA_LIFESPAN and NULL data that was skipped over in the above query
	sprintf(query, "DELETE FROM meter_data WHERE meter_id = %d AND resolution = '%s' AND ((recorded < %d) OR (recorded >= %d AND value IS NULL))", meter_id, TARGET_RES, (int) end_time - DATA_LIFESPAN, (int) last_recording);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) {
		error(mysql_error(conn), conn);
	}
	// insert new data
	double last_non_null = -9999.0; // error value
	for (int i = 0; i < cJSON_GetArraySize(data); i++) {
		cJSON *data_point = cJSON_GetArrayItem(data, i);
		cJSON *data_point_val = cJSON_GetObjectItem(data_point, "value");
		cJSON *data_point_time = cJSON_GetObjectItem(data_point, "localtime");
		char val[10];
		if (data_point_val->type == 4) {
			val[0] = 'N'; val[1] = 'U'; val[2] = 'L'; val[3] = 'L'; val[4] = '\0'; // srsly?
		} else {
			last_non_null = data_point_val->valuedouble;
			sprintf(val, "%f", last_non_null);
		}
		// https://stackoverflow.com/a/1002631/2624391
		struct tm ltm = {0};
		time_t epoch = 0;
		if (strptime(data_point_time->valuestring, ISO8601_FORMAT, &ltm) != NULL) {
			epoch = mktime(&ltm);
		} else {
			error("Unable to parse date", conn);
		}
		sprintf(sql_data, "INSERT INTO meter_data (meter_id, value, recorded, resolution) VALUES (%d, %s, %d, '%s')", meter_id, val, (int) epoch, TARGET_RES);
		if (READONLY_MODE == 0 && mysql_query(conn, sql_data)) {
			error(mysql_error(conn), conn);
		}
	}
	sprintf(query, UPDATE_TIMESTAMP_COL, (int) end_time, meter_id);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) {
		error(mysql_error(conn), conn);
	}
	#if UPDATE_CURRENT == 1
	if (last_non_null != -9999.0) {
		query[0] = '\0';
		sprintf(query, "UPDATE meters SET current = %f WHERE id = %d", last_non_null, meter_id);
		if (READONLY_MODE == 0 && mysql_query(conn, query)) {
			error(mysql_error(conn), conn);
		}
	}
	#endif
}

int main(void) {
	daemonize();
	buildingosd_pid = getpid();
	syslog(LOG_NOTICE, "starting daemon %d", buildingosd_pid);
	api_token = malloc(40*sizeof(char));
	api_token[0] = '\0';
	MYSQL *conn;
	conn = mysql_init(NULL);
	// Connect to database
	if (!mysql_real_connect(conn, DB_SERVER,
	DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
		error(mysql_error(conn), conn);
	}
	// Insert record of daemon
	char query[SMALL_CONTAINER];
	char query2[SMALL_CONTAINER];
	sprintf(query, "INSERT INTO daemons (pid, enabled, target_res) VALUES (%d, %d, '%s')", buildingosd_pid, 1, TARGET_RES);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) { // short circuit
		error(mysql_error(conn), conn);
	}
	sprintf(query, "SELECT enabled FROM daemons WHERE pid = %d", buildingosd_pid);
	while (1) {
		MYSQL_RES *res;
		MYSQL_ROW row;
		MYSQL_ROW meter;
		if (READONLY_MODE == 0) {
			if (mysql_query(conn, query)) {
				error(mysql_error(conn), conn);
			}
			res = mysql_use_result(conn);
			row = mysql_fetch_row(res);
			mysql_free_result(res);
			if (row == NULL) {
				error("I should not exist", conn);
			} else if (row[0][0] == '0') { //(strcmp(row[0], "1") != 0) {
				syslog(LOG_NOTICE, "Enabled column switched off");
				cleanup(conn); // if enabled column turned off, exit
			}
		}
		char meter_url[SMALL_CONTAINER];
		meter_url[0] = '\0';
		meter = fetch_row(conn, TARGET_METER);
		int meter_id = atoi(meter[0]);
		char *org_id = meter[1];
		strcat(meter_url, meter[2]);
		strcat(meter_url, "/data");
		int last_updated = atoi(meter[3]);
		set_api_token(conn, org_id);
		sprintf(query2, "UPDATE daemons SET updating_meter = %d WHERE pid = %d", meter_id, buildingosd_pid);
		if (READONLY_MODE == 0) {
			if (mysql_query(conn, query2)) {
				error(mysql_error(conn), conn);
			}
		}
		struct timeval tv;
		gettimeofday(&tv, NULL);
		int time = tv.tv_sec;
		if (last_updated > (time - MOVE_BACK_AMOUNT)) {
			sleep(20);
		}
		pid_t childpid = fork();
		if (childpid == -1) {
			error("Failed to fork", conn);
		} 
		else if (childpid > 0) {
			int status;
			waitpid(childpid, &status, 0);
		} else  { // we are the child
			update_meter(conn, meter_id, meter_url);
			exit(1);
		}
		// break;
	}
	cleanup(conn);
	mysql_close(conn);
	return EXIT_SUCCESS;
}
