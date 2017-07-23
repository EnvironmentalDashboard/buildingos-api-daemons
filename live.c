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
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <netdb.h>
#include "./lib/cJSON/cJSON.h"
#include "db.h"
#define HTTP_RESPONSE_SIZE 4096
#define TARGET_RES "live"
#define TARGET_METER "SELECT id, org_id, bos_uuid, url, live_last_updated FROM meters WHERE (gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '') AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') AND source = 'buildingos' ORDER BY live_last_updated ASC LIMIT 1"
#define TOKEN_URL "https://api.buildingos.com/o/token/"
#define h_addr h_addr_list[0] // not sure why I need this
#define DEBUG 1

// Stores last page downloaded by http_request()
struct MemoryStruct {
	char *memory;
	size_t size;
};

/**
 * Helper for http_request()
 */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;
 
	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if(mem->memory == NULL) {
		/* out of memory! */ 
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
void http_request(char *url, char *post) {
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
		else {
			/*
			 * Now, our chunk.memory points to a memory block that is chunk.size
			 * bytes big and contains the remote file.
			 *
			 * Do something nice with it!
			 */ 
			printf("%s\n", chunk.memory);
		}
		/* always cleanup */ 
		curl_easy_cleanup(curl);
		free(chunk.memory);
		/* we're done with libcurl, so clean it up */ 
		curl_global_cleanup();
	}
}

void cleanup(MYSQL *conn, pid_t pid) {
	char query[255];
	sprintf(query, "DELETE FROM daemons WHERE pid = %d", pid);
	if (mysql_query(conn, query)) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
}

void error(const char *msg) {
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

/**
 * Fetches a single record
 * @param  conn  [description]
 * @param  query [description]
 * @return       [description]
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

void update_meter(MYSQL *conn) {
	MYSQL_RES *res;
	MYSQL_ROW row;
	/* send SQL query */
	if (mysql_query(conn, "show tables")) {
		fprintf(stderr, "%s\n", mysql_error(conn));
		exit(1);
	}
	res = mysql_use_result(conn);

	/* output table name */
	printf("MySQL Tables in mysql database:\n");
	while ((row = mysql_fetch_row(res)) != NULL)
		printf("%s \n", row[0]);

	mysql_free_result(res);
}

char *api_token(MYSQL *conn, char *org_id) {
	char query[255];
	MYSQL_ROW row;
	sprintf(query, "SELECT api_id FROM orgs WHERE id = %s", org_id);
	int api_id = atoi(fetch_row(conn, query)[0]);
	sprintf(query, "SELECT token, token_updated FROM api WHERE id = %d", api_id);
	row = fetch_row(conn, query);
	int update_token_at = atoi(row[1]) + 3595;
	if (0 && update_token_at > (int) time(NULL)) { // token still not expired
		return row[0];
	} else { // amortized cost; need to get new API token
		sprintf(query, "SELECT client_id, client_secret, username, password FROM api WHERE id = '%d'", api_id);
		row = fetch_row(conn, query);
		char post_data[255];
		sprintf(post_data, "client_id=%s&client_secret=%s&username=%s&password=%s&grant_type=password", row[0], row[1], row[2], row[3]);
		http_request(TOKEN_URL, post_data);
	}
	return "";
}

int main(void) {
	// cJSON * root = cJSON_Parse("{ \"name\" : \"Jack\", \"age\" : 27 }");
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
	char query[255];
	sprintf(query, "INSERT INTO daemons (pid, enabled, target_res) VALUES (%d, %d, '%s')", pid, 0, TARGET_RES);
	if (DEBUG == 0 && mysql_query(conn, query)) { // short circuit
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
		if (DEBUG == 0) {
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
		// if (strcmp(meter[0], "")==0)
			// printf("%s\n", "i thought so");
		char *org_id = meter[1];
		printf("%s - %s - %s - %s - %s\n", meter[0], org_id, meter[2], meter[3], meter[4]);
		// exit(1);
		api_token(conn, org_id);
		// updateMeter(conn);
		break;
	}
	mysql_close(conn);
	return 0;
}
