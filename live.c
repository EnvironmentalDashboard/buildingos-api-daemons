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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "./lib/cJSON/cJSON.h"
#include "db.h"
#define HTTP_RESPONSE_SIZE 10000
#define TARGET_RES "live"
#define TARGET_METER "SELECT id, org_id, bos_uuid, url, live_last_updated FROM meters WHERE (gauges_using > 0 OR for_orb > 0 OR timeseries_using > 0) OR bos_uuid IN (SELECT DISTINCT meter_uuid FROM relative_values WHERE permission = 'orb_server' AND meter_uuid != '') AND id NOT IN (SELECT updating_meter FROM daemons WHERE target_res = 'live') AND source = 'buildingos' ORDER BY live_last_updated ASC LIMIT 1"
#define h_addr h_addr_list[0] // not sure why I need this
#define DEBUG 1

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
 * Sends an HTTP POST request and returns the response
 * See https://stackoverflow.com/a/22135885
 * @param  host    e.g. api.somesite.com
 * @param  message e.g. "POST /apikey=%s&command=%s HTTP/1.0\r\n\r\n"
 * @return         response from requested resource
 */
char *http_post(char *host, char *message) {
  int portno = 80;
  struct hostent *server;
  struct sockaddr_in serv_addr;
  int sockfd, bytes, sent, received, total;
  char *response = malloc(HTTP_RESPONSE_SIZE * sizeof(char));
  /* create the socket */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) error("ERROR opening socket");
  /* lookup the ip address */
  server = gethostbyname(host);
  if (server == NULL) error("ERROR, no such host");
  /* fill in the structure */
  memset(&serv_addr,0,sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(portno);
  memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);
  /* connect the socket */
  if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
    error("ERROR connecting");
  }
  /* send the request */
  total = strlen(message);
  sent = 0;
  do {
    bytes = write(sockfd,message+sent,total-sent);
    if (bytes < 0) {
      error("ERROR writing message to socket");
    }
    if (bytes == 0) {
      break;
    }
    sent+=bytes;
  } while (sent < total);
  /* receive the response */
  memset(response,0,sizeof(response));
  total = sizeof(response)-1;
  received = 0;
  do {
    bytes = read(sockfd,response+received,total-received);
    if (bytes < 0) {
      error("ERROR reading response from socket");
    }
    if (bytes == 0) {
      break;
    }
    received+=bytes;
  } while (received < total);

  if (received == total) {
  	printf("%s\n", response);
    error("ERROR storing complete response from socket");
  }
  /* close the socket */
  close(sockfd);
  return response;
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
		char token_url[1024];
		sprintf(token_url, "POST /o/token?client_id=%s&client_secret=%s&username=%s&password=%s&grant_type=password HTTP/1.0\r\n\r\n", row[0], row[1], row[2], row[3]);
		printf("%s\n", http_post("api.buildingos.com", token_url));
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
