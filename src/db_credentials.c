//
// Created by ryuzot on 24/12/26.
//

#include "db_credentials.h"

void free_credentials(DbCredentials *creds) {
    if (creds) {
        free(creds->host);
        free(creds->port);
        free(creds->user);
        free(creds->password);
        free(creds->dbname);
        free(creds); // 構造体自体もfree
    }
}

DbCredentials * get_db_credentials() {
    DbCredentials *creds = (DbCredentials *)malloc(sizeof(DbCredentials));
    if (!creds) {
        perror("Failed to allocate memory for credentials");
        return NULL;
    }

    creds->host = get_env_variable("MOBAKU_DB_HOST");
    creds->port = get_env_variable("MOBAKU_DB_PORT");
    creds->user = get_env_variable("MOBAKU_DB_USER");
    creds->password = get_env_variable("MOBAKU_DB_PASSWORD");
    creds->dbname = get_env_variable("MOBAKU_DB_NAME");

    if (!creds->host || !creds->port || !creds->user || !creds->password || !creds->dbname) {
        fprintf(stderr, "Missing required environment variables.\n");
        free_credentials(creds); // 取得に失敗したら確保した領域を解放
        return NULL;
    }
    return creds;
}
