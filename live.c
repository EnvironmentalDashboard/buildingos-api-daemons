/**
 * Updates live data for meters
 *
 * @author Tim Robert-Fitzgerald
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <mysql.h>
#include <sys/types.h>
#include <stdlib.h>
#include <curl/curl.h> // install with `apt-get install libcurl4-openssl-dev`
#include <curl/easy.h>
#include "./lib/cJSON/cJSON.h"
#include "db.h"
#define TARGET_RES "live"
#define TARGET_METER "SELECT id, org_id, bos_uuid, url, live_last_updated FROM meters WHERE (gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '') AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') AND source = 'buildingos' ORDER BY live_last_updated ASC LIMIT 1"
#define UPDATE_METER_TIMESTAMP "UPDATE meters SET live_last_updated = %d WHERE id = %d"
#define TOKEN_URL "https://api.buildingos.com/o/token/"
#define ISO8601_FORMAT "%Y-%m-%dT%H:%M:%S-04:00"
#define TRACK_DAEMON 1 // whether to save record of this daemon in db
#define SMALL_CONTAINER 255
#define MOVE_BACK_AMOUNT 180 // meant to move meters back in the queue of what's being updated by update_meter() so they don't hold up everything if update_meter() keeps failing for some reason. note that if update_meter() does finish, it pushes the meter to the end of the queue by updating the last_updated_col to the current time otherwise the last_updated_col remains the current time minus this amount
#define DATA_LIFESPAN 7200 // live data is stored for 2 hours i.e. 7200s
static char *api_token;

// Stores last page downloaded by http_post()
struct MemoryStruct {
	char *memory;
	size_t size;
};

/**
 * Helper for http_post()
 */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;
	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory == NULL) {
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	return realsize;
}

/**
 * See https://curl.haxx.se/libcurl/c/postinmemory.html
 * @param url  http://www.example.org/
 * @param post e.g. Field=1&Field=2&Field=3
 */
struct MemoryStruct http_post(char *url, char *post) {
	CURL *curl;
	CURLcode res;
	struct MemoryStruct chunk;
	chunk.memory = malloc(1);  /* will be grown as needed by realloc above */ 
	chunk.size = 0;    /* no data at this point */ 
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		/* send all data to this function  */ 
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		/* we pass our 'chunk' struct to the callback function */ 
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
		/* some servers don't like requests that are made without a user-agent
			 field, so we provide one */ 
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
		/* if we don't provide POSTFIELDSIZE, libcurl will strlen() by
			 itself */ 
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(post));
		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);
		/* Check for errors */ 
		if (res != CURLE_OK) {
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
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
void cleanup(MYSQL *conn, pid_t pid) {
	char query[SMALL_CONTAINER];
	sprintf(query, "DELETE FROM daemons WHERE pid = %d", pid);
	if (mysql_query(conn, query)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
}

/**
 * Handle errors
 */
void error(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

/**
 * Fetches a single record
 */
MYSQL_ROW fetch_row(MYSQL *conn, char *query) {
	MYSQL_RES *res;
	MYSQL_ROW row;
	if (mysql_query(conn, query)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
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
		struct MemoryStruct response = http_post(TOKEN_URL, post_data);
		cJSON *root = cJSON_Parse(response.memory);
		cJSON *access_token = cJSON_GetObjectItem(root, "access_token");
		api_token = (char*) access_token->valuestring;
		sprintf(query, "UPDATE api SET token = '%s', token_updated = %d WHERE id = %d", api_token, time, api_id);
		if (mysql_query(conn, query)) {
			fprintf(stderr, "%s\n", mysql_error(conn));
			exit(1);
		}
		free(response.memory);
		cJSON_free(root);
	}
}

/**
 * Updates a meter
 * @param conn       [description]
 * @param meter_id   [description]
 * @param meter_uuid [description]
 * @param meter_url  [description]
 */
void update_meter(MYSQL *conn, int meter_id, char *meter_uuid, char *meter_url) {
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
	sprintf(query, UPDATE_METER_TIMESTAMP, (int) end_time - MOVE_BACK_AMOUNT, meter_id);
	if (0 && mysql_query(conn, query)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
	// Get the most recent recording. Data fetched from the API will start at last_recording and end at time
	MYSQL_RES *res;
	MYSQL_ROW row;
	sprintf(query, "SELECT recorded FROM meter_data WHERE meter_id = %d AND resolution = '%s' AND value IS NOT NULL ORDER BY recorded DESC LIMIT 1", meter_id, TARGET_RES);
	if (mysql_query(conn, query)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
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
	sprintf(post_data, "resolution=%s&start=%s&end=%s", TARGET_RES, iso8601_last_recording, iso8601_time);
	struct MemoryStruct response = http_post(meter_url, post_data);
}

int main(void) {
	api_token = malloc(40*sizeof(char));
	MYSQL *conn;
	pid_t pid = getpid();
	conn = mysql_init(NULL);
	// Connect to database
	if (!mysql_real_connect(conn, DB_SERVER,
	DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
	// Insert record of daemon
	char query[SMALL_CONTAINER];
	sprintf(query, "INSERT INTO daemons (pid, enabled, target_res) VALUES (%d, %d, '%s')", pid, 0, TARGET_RES);
	if (TRACK_DAEMON == 0 && mysql_query(conn, query)) { // short circuit
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
	sprintf(query, "SELECT enabled FROM daemons WHERE pid = %d", pid);
	while (1) {
		MYSQL_RES *res;
		MYSQL_ROW row;
		MYSQL_ROW meter;
		if (mysql_query(conn, query)) {
			fprintf(stderr, "%s\n", mysql_error(conn));
			exit(1);
		}
		res = mysql_use_result(conn);
		row = mysql_fetch_row(res);
		mysql_free_result(res);
		if (TRACK_DAEMON == 0) {
			if (row == NULL) {
				fprintf(stderr, "I should not exist.\n");
				exit(1);
			} else if (strcmp(row[0], "1") != 0) {
				fprintf(stderr, "enabled column switched off\n");
				cleanup(conn, pid);
				break; // if enabled column turned off, exit
			}
		}
		meter = fetch_row(conn, TARGET_METER);
		int meter_id = atoi(meter[0]);
		char *org_id = meter[1];
		char *meter_uuid = meter[2];
		char *meter_url_base = meter[3];
		int last_updated = atoi(meter[4]);
		set_api_token(conn, org_id);
		char meter_url[SMALL_CONTAINER];
		strcpy(meter_url, meter_url_base);
		strcat(meter_url, "/data");
		update_meter(conn, meter_id, meter_uuid, meter_url);
		break;
	}
	mysql_close(conn);
	return 0;
}
