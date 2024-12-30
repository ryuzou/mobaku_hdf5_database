//
// Created by ryuzot on 24/12/26.
//

#ifndef DB_CREDENTIALS_H
#define DB_CREDENTIALS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "env_reader.h"

typedef struct {
    char *host;
    char *port;
    char *user;
    char *password;
    char *dbname;
} DbCredentials;

void free_credentials(DbCredentials *creds);

DbCredentials* get_db_credentials();
#endif //DB_CREDENTIALS_H
