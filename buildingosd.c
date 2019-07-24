/**
 * Fetches data from the BuildingOS API and caches it in the db
 *
 * @author Tim Robert-Fitzgerald
 */

#define _XOPEN_SOURCE // for strptime
#define _GNU_SOURCE // for strptime
#define TARGET_METER "SELECT id, url, last_updated FROM meter WHERE source = 'buildingos' AND (gauges_using > 0 OR charts_using > 0) AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') ORDER BY last_updated ASC, id ASC LIMIT 1"
#define UPDATE_TIMESTAMP "UPDATE meter SET last_updated = %d WHERE id = %d"
#define TOKEN_URL "https://api.buildingos.com/o/token/" // where to get the token from
#define ISO8601_FORMAT "%Y-%m-%dT%H:%M:%S%z"
#define BUFFER_FILE "/root/meter_data.csv"
#define SMALL_CONTAINER 255 // small fixed-size container for arrays
#define MED_CONTAINER 510 // just double SMALL_CONTAINER
#define DATA_LIFESPAN 7200 // live data is stored for 2 hours i.e. 7200s
#define MOVE_BACK_AMOUNT 180 // meant to move meters back in the queue of what's being updated by update_meter() so they don't hold up everything if update_meter() keeps failing for some reason. note that if update_meter() does finish, it pushes the meter to the end of the queue by updating the last_updated_col to the current time otherwise the last_updated_col remains the current time minus this amount.
#define UPDATE_CURRENT 1 // update the meters.current column with the current reading?
#define READONLY_MODE 0 // if on (i.e. 1) the daemon will not make queries that update/insert/delete data by short circuiting if stmts

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <mysql.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <curl/curl.h> // install with `apt-get install libcurl4-openssl-dev`
#include <curl/easy.h>
#include "lib/cJSON/cJSON.h"

static char hostname[20]; // docker hostnames are 12 chars so 20 should be more than enough
static char *client_id;
static char *client_secret;
static char *username;
static char *password;
static char *db_server;
static char *db_user;
static char *db_pass;
static char *db_name;
unsigned int db_port;

// Stores page downloaded by http_request()
struct MemoryStruct {
	char *memory;
	size_t size;
};

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
	fprintf(stderr, "Caught pipe #%d; exiting", signo);
}

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
	char header[SMALL_CONTAINER];
	if (custom_header) {
		snprintf(header, sizeof(header), "Authorization: Bearer %s", api_token);
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
			fprintf(stderr, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
			exit(1);
		}
		curl_easy_cleanup(curl);
		curl_global_cleanup();
	}
	return chunk;//chunk.memory;
	// free(chunk.memory);
}

/**
 * Execute before program termination
 */
void cleanup(MYSQL *conn) {
	char query[SMALL_CONTAINER];
	snprintf(query, sizeof(query), "DELETE FROM daemons WHERE host = '%s'", hostname);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) {
		fprintf(stderr, "%s", mysql_error(conn));
	}
	mysql_close(conn);
	exit(1);
}

/**
 * Handle errors
 */
void error(const char *msg, MYSQL *conn) {
	fprintf(stderr, "%s", msg);
	cleanup(conn);
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
	res = mysql_store_result(conn);
	row = mysql_fetch_row(res);
	// mysql_free_result(res);
	if (row == NULL) {
		fprintf(stderr, "QUERY '%s' RETURNED 0 ROWS\n", query);
		cleanup(conn);
	}
	return row;
}

/**
 * Sets the API token, fetching a new one if necessary
 * @param conn
 */
char *set_api_token(MYSQL *conn) {
	char query[SMALL_CONTAINER];
	MYSQL_ROW row;
	// use from_unixtime, UNIX_TIMESTAMP
	snprintf(query, sizeof(query), "SELECT token, UNIX_TIMESTAMP(token_updated) FROM buildingos_api WHERE client_id = '%s'", client_id);
	row = fetch_row(conn, query);
	int update_token_at = atoi(row[1]) + 3595;
	time_t epoch = time(NULL);
	struct tm *tm = localtime(&epoch);
	int time = (int) mktime(tm);
	if (update_token_at > time) { // token still not expired
		return row[0]; // Invalid read of size 8
	} else { // amortized cost; need to get new API token
		char post_data[MED_CONTAINER];
		snprintf(post_data, sizeof(post_data), "client_id=%s&client_secret=%s&username=%s&password=%s&grant_type=password", client_id, client_secret, username, password);
		struct MemoryStruct response = http_request(TOKEN_URL, post_data, 0, 1, "");
		cJSON *root = cJSON_Parse(response.memory);
		cJSON *access_token = cJSON_GetObjectItem(root, "access_token");
		char *api_token = access_token->valuestring;
		snprintf(query, sizeof(query), "UPDATE buildingos_api SET token = '%s', token_updated = FROM_UNIXTIME(%d) WHERE client_id = '%s'", api_token, time, client_id);
		if (mysql_query(conn, query)) { // do this even if READONLY_MODE is on bc it cant hurt to update the api token
			error(mysql_error(conn), conn);
		}
		free(response.memory);
		cJSON_Delete(root);
		return api_token;
	}
}

/**
 * Updates a meter
 * this function has way too many parameters, but it's better than globals
 * @param conn
 * @param meter_id
 * @param meter_url
 * @param start_time    the earlier date
 * @param end_time      the later date
 */
void update_meter(MYSQL *conn, int meter_id, char *meter_url, char *api_token, time_t start_time, time_t end_time, int verbose) {
	struct tm *ts;
	char iso8601_end_time[25];
	char iso8601_start_time[25];
	char query[SMALL_CONTAINER];
	char tmp_buffer[SMALL_CONTAINER];
	// API requires ISO-8601 format with a ':' seperator in the timezone offset
	ts = localtime(&end_time);
	strftime(iso8601_end_time, sizeof(iso8601_end_time), ISO8601_FORMAT, ts);
	iso8601_end_time[22] = ':'; iso8601_end_time[23] = '0'; iso8601_end_time[24] = '0'; iso8601_end_time[25] = '\0';
	ts = localtime(&start_time);
	strftime(iso8601_start_time, sizeof(iso8601_start_time), ISO8601_FORMAT, ts);
	iso8601_start_time[22] = ':'; iso8601_start_time[23] = '0'; iso8601_start_time[24] = '0'; iso8601_start_time[25] = '\0';
	// Make call to the API for meter data
	char post_data[SMALL_CONTAINER];
	char *encoded_iso8601_start_time = str_replace(iso8601_start_time, ":", "%3A");
	char *encoded_iso8601_end_time = str_replace(iso8601_end_time, ":", "%3A");
	snprintf(post_data, sizeof(post_data), "resolution=1&start=%s&end=%s", encoded_iso8601_start_time, encoded_iso8601_end_time);
	free(encoded_iso8601_start_time);
	free(encoded_iso8601_end_time);
	struct MemoryStruct response = http_request(meter_url, post_data, 1, 0, api_token);
	cJSON *root = cJSON_Parse(response.memory);
	if (!cJSON_HasObjectItem(root, "data")) {
		error(response.memory, conn);
	}
	cJSON *data = cJSON_GetObjectItem(root, "data");
	// save new data
	FILE *buffer = fopen(BUFFER_FILE, "a");
	if (buffer == NULL) {
	    error("Error opening meter_data buffer", conn);
	}
	int data_size = cJSON_GetArraySize(data);
	double last_non_null = -9999.0; // error value
	for (int i = 0; i < data_size; i++) {
		cJSON *data_point = cJSON_GetArrayItem(data, i);
		cJSON *data_point_val = cJSON_GetObjectItem(data_point, "value");
		cJSON *data_point_time = cJSON_GetObjectItem(data_point, "localtime");
		char val[10];
		if (data_point_val->type == 4) {
			val[0] = '\\'; val[1] = 'N'; val[2] = '\0'; // https://stackoverflow.com/a/2675493
		} else {
			last_non_null = data_point_val->valuedouble;
			snprintf(val, sizeof(val), "%.3f", last_non_null);
		}
		// https://stackoverflow.com/a/1002631/2624391
		struct tm tm = {0};
		time_t epoch = 0;
		if (strptime(data_point_time->valuestring, ISO8601_FORMAT, &tm) != NULL) {
			tm.tm_isdst = -1; // Is DST on? 1 = yes, 0 = no, -1 = unknown
			// epoch = mktime(&tm);
			// See http://kbyanc.blogspot.com/2007/06/c-converting-struct-tm-times-with.html for why mktime() does not work
			struct tm tmp;
            tmp = tm;
            epoch = timegm(&tmp) - (tm.tm_gmtoff);
		} else {
			error("Unable to parse date", conn);
		}
		snprintf(tmp_buffer, sizeof(tmp_buffer), "%d,%s,%d,\"1\"\n", meter_id, val, (int) epoch);
		fputs(tmp_buffer, buffer);
		/*
		if (fprintf(buffer, "%d,%s,%d,\"1\"\n", meter_id, val, (int) epoch) < 0) { // sometimes doesn't work
			error("Unable to write data", conn);
		}
		if (fflush(buffer) != 0) {
			error("Unable to flush data", conn);	
		}
		*/
		if (verbose) {
			printf("%d,%s,%d,\"1\"\n", meter_id, val, (int) epoch);
		}
	}
	fclose(buffer);
	free(response.memory);
	cJSON_Delete(root);
	#if UPDATE_CURRENT == 1
	if (last_non_null != -9999.0) {
		query[0] = '\0';
		snprintf(query, sizeof(query), "UPDATE meters SET current = %.3f WHERE id = %d", last_non_null, meter_id);
		if (READONLY_MODE == 0 && mysql_query(conn, query)) {
			error(mysql_error(conn), conn);
		}
	}
	#endif
}

int main(int argc, char *argv[]) {
	// data fetched spans from start_time to end_time
	time_t end_time;
	time_t start_time;
	int opt;
	char tmp[SMALL_CONTAINER];
	// If the -o flag is set, the program will update a single meter instead of looping
	int o_flag = 0;
	// -v flag prints debugging information
	int v_flag = 0;
	// -t to switch the time span the data is fetched for
	int t_flag = 0;
	while ((opt = getopt (argc, argv, "ovt")) != -1) {
		switch (opt) {
			case 'o': // run "once"
				o_flag = 1;
				break;
			case 'v': // "verbose"
				v_flag = 1;
				break;
			case 't': // "time"
				t_flag = 1;
				break;
		}
	}
	gethostname(hostname, 20);
	client_id = getenv("CLIENT_ID");
	client_secret = getenv("CLIENT_SECRET");
	username = getenv("USERNAME");
	password = getenv("PASSWORD");
	db_server = getenv("DB_SERVER");
	db_user = getenv("DB_USER");
	db_pass = getenv("DB_PASS");
	db_name = getenv("DB_NAME");
	db_port = atoi(getenv("DB_PORT"));
	// connect to db
	MYSQL *conn;
	conn = mysql_init(NULL);
	if (!mysql_real_connect(conn, db_server,
	db_user, db_pass, db_name, db_port, NULL, 0)) {
		error(mysql_error(conn), conn);
	}
	// insert record of daemon
	char query[SMALL_CONTAINER];
	snprintf(query, sizeof(query), "REPLACE INTO daemons (enabled, host) VALUES (%d, '%s')", 1, hostname);
	if (READONLY_MODE == 0 && mysql_query(conn, query)) { // short circuit
		error(mysql_error(conn), conn);
	}
	signal(SIGPIPE, catch_signal);
	snprintf(query, sizeof(query), "SELECT enabled FROM daemons WHERE host = '%s'", hostname); // dont modify query variable again!
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
			if (row == NULL) { // record of daemon does not exist
				error("Program record not found; exiting", conn);
			} else if (row[0][0] != '1') {
				// if enabled column turned off, exit
				puts("Enabled column switched off");
				cleanup(conn);
			}
			mysql_free_result(res);
		}
		meter = fetch_row(conn, TARGET_METER);
		char meter_url[SMALL_CONTAINER];
		meter_url[0] = '\0';
		int meter_id = atoi(meter[0]);
		strcat(meter_url, meter[1]);
		strcat(meter_url, "/data");
		int last_updated = atoi(meter[2]);
		snprintf(tmp, sizeof(tmp), "UPDATE daemons SET updating_meter = %d WHERE host = '%s'", meter_id, hostname);
		if (READONLY_MODE == 0) {
			if (mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
		}
		if (last_updated > (now - 60)) { // if the least up to date meter was last updated 60 seconds ago
			sleep(now - last_updated);
		}
		// Set start/end time
		if (t_flag) {
			// only make sure data goes back as far as it's supposed to
			// i.e. fetch data spanning from DATA_LIFESPAN to the earliest point recorded in the db
			start_time = now - (time_t) DATA_LIFESPAN;
			snprintf(tmp, sizeof(tmp), "SELECT recorded FROM reading WHERE meter_id = %d AND value IS NOT NULL ORDER BY recorded ASC LIMIT 1", meter_id);
			if (mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
			res = mysql_store_result(conn);
			row = mysql_fetch_row(res);
			if (row == NULL) { // no data exists for this meter
				end_time = now;
				mysql_free_result(res);
			} else {
				end_time = (time_t) atoi(row[0]);
				mysql_free_result(res);
				if (end_time < ((now - DATA_LIFESPAN) + 60)) { // if the end time goes as far back as we store data for, mark meter as updated and continue
					snprintf(tmp, sizeof(tmp), UPDATE_TIMESTAMP, (int) now, meter_id);
					if (READONLY_MODE == 0 && mysql_query(conn, tmp)) {
						error(mysql_error(conn), conn);
					}
					continue;
				}
			}
		} else {
			// fetch data spanning from the latest point recorded in the db to now
			end_time = now;
			snprintf(tmp, sizeof(tmp), "SELECT recorded FROM reading WHERE meter_id = %d AND value IS NOT NULL ORDER BY recorded DESC LIMIT 1", meter_id);
			if (mysql_query(conn, tmp)) {
				error(mysql_error(conn), conn);
			}
			res = mysql_store_result(conn);
			row = mysql_fetch_row(res);
			if (row == NULL) { // no data exists for this meter
				start_time = end_time - (time_t) DATA_LIFESPAN;
			} else {
				start_time = (time_t) atoi(row[0]);
			}
			mysql_free_result(res);
		}
		snprintf(tmp, sizeof(tmp), UPDATE_TIMESTAMP, (int) now - MOVE_BACK_AMOUNT, meter_id);
		if (READONLY_MODE == 0 && mysql_query(conn, tmp)) {
			error(mysql_error(conn), conn);
		}
		update_meter(conn, meter_id, meter_url, set_api_token(conn), start_time, end_time, v_flag);
		snprintf(tmp, sizeof(tmp), UPDATE_TIMESTAMP, (int) now, meter_id);
		if (READONLY_MODE == 0 && mysql_query(conn, tmp)) {
			error(mysql_error(conn), conn);
		}
		if (v_flag == 1) {
			printf("Updated meter %d (fetched data from %d to %d)\n", meter_id, (int) start_time, (int) end_time);
		}
		if (o_flag == 1) {
			break;
		}
	}
	cleanup(conn);
	mysql_close(conn);
	return EXIT_SUCCESS;
}
