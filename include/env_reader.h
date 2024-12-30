//
// Created by ryuzot on 24/12/26.
//

#ifndef ENV_READER_H
#define ENV_READER_H
#include <stdio.h>
#include <stdlib.h>

#include <stdbool.h>

// .envファイルを読み込む関数。成功したらtrue、失敗したらfalseを返す。
bool load_env_from_file(const char* filepath);

// 環境変数の値を取得する関数 (変更なし)
char* get_env_variable(const char* name);
#endif //ENV_READER_H
