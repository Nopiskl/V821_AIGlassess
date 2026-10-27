#ifndef AIGLASS_JSON_H
#define AIGLASS_JSON_H
#include "third_party/cJSON.h"
/* Serializes cJSON's shared parser error state. Do not call GetErrorPtr(). */
cJSON *ag_json_parse(const char *text);
#endif
