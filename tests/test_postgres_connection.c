#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <libpq-fe.h>
#include "env_reader.h"
#include "db_credentials.h"

// ==== メイン関数 ====================================================
int main(int argc, char* argv[]) {

    const char* env_filepath = ".env";
    if (argc > 1) {
        env_filepath = argv[1];
    }

    if (!load_env_from_file(env_filepath)) {
        fprintf(stderr, "Failed to load environment from %s\n", env_filepath);
        return 1;
    }
    DbCredentials *creds = get_db_credentials();

    if (!creds) {
        return 1;
    }

    char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s",
             creds->host, creds->port, creds->dbname, creds->user, creds->password);


    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        free_credentials(creds);
        return 1;
    }
    PQfinish(conn);

    printf("Successfully connected to PostgreSQL!\n");

    return 0;
}
