//
// Created by ryuzot on 24/12/26.
//

#include "env_reader.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

bool load_env_from_file(const char* filepath) {
    // 絶対パスを取得
    char absolute_path[PATH_MAX];

    // realpath()を使用して絶対パスを取得
    if (realpath(filepath, absolute_path) == NULL) {
        // realpath()で絶対パスを取得できない場合は、カレントディレクトリを使用
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(absolute_path, sizeof(absolute_path), "%s/%s", cwd, filepath);
        } else {
            // カレントディレクトリも取得できない場合は元のファイルパスを使用
            strncpy(absolute_path, filepath, sizeof(absolute_path));
        }
    }

    FILE* file = fopen(filepath, "r");
    if (file == NULL) {
        // ファイルを開けない場合に絶対パスを表示
        fprintf(stderr, "Error opening .env file: %s\n", absolute_path);
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // 以下、元のコードと同じ
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\n' || *start == '#') continue;

        char* eq = strchr(start, '=');
        if (eq == NULL) continue;

        *eq = '\0';
        char* key = start;
        char* value = eq + 1;

        while (*value == ' ' || *value == '\t') value++;
        size_t value_len = strlen(value);
        while (value_len > 0 && (value[value_len - 1] == ' ' || value[value_len - 1] == '\t' || value[value_len - 1] == '\n' || value[value_len - 1] == '\r')) {
            value[--value_len] = '\0';
        }

        if (setenv(key, value, 1) != 0) {
            perror("setenv failed");
            fclose(file);
            return false;
        }
    }

    fclose(file);
    return true;
}

char* get_env_variable(const char* name) {
    char* value = getenv(name);
    if (value == NULL) {
        fprintf(stderr, "Environment variable %s not found.\n", name);
        return NULL;
    }

    char* duplicated_value = strdup(value);
    if (duplicated_value == NULL) {
        perror("strdup failed");
        return NULL;
    }
    return duplicated_value;
}