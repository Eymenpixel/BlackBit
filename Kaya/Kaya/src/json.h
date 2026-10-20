#ifndef KAYA_JSON_H
#define KAYA_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type;

typedef struct json_value {
    json_type type;
    union {
        int boolean;
        double number;
        char *string;
        struct { struct json_value **items; size_t count; } array;
        struct { char **keys; struct json_value **values; size_t count; } object;
    } u;
} json_value;

/* Parse a JSON document. Returns NULL on error (err, if non-NULL, gets a
 * short static description). Caller owns the returned tree and must call
 * json_free() on it. */
json_value *json_parse(const char *text, const char **err);
void json_free(json_value *v);

/* Construction and serialization helpers. Containers own values after a
 * successful set/append. json_stringify returns a malloc'd NUL-terminated
 * string owned by the caller. */
json_value *json_new_object(void);
json_value *json_new_array(void);
json_value *json_new_string(const char *s);
json_value *json_new_number(double n);
int json_object_set(json_value *obj, const char *key, json_value *value);
int json_array_append(json_value *array, json_value *value);
char *json_stringify(const json_value *v);

/* Object helpers. All returned pointers/strings are owned by the tree
 * (valid until json_free) except where noted. */
json_value *json_object_get(const json_value *obj, const char *key);
const char *json_get_string(const json_value *obj, const char *key, const char *def);
double json_get_number(const json_value *obj, const char *key, double def);

/* Returns a newly-malloc'd array of `count` const char* pointing into the
 * tree's own strings (do not free the strings, but free() the array itself).
 * Returns NULL and sets count=0 if the key is missing or not an array. */
const char **json_get_string_array(const json_value *obj, const char *key, size_t *count);

#endif
