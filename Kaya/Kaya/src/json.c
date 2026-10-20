#define _POSIX_C_SOURCE 200809L
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const char *p;
    const char *err;
} parser;

static void skip_ws(parser *ps) {
    while (*ps->p && isspace((unsigned char)*ps->p)) ps->p++;
}

static json_value *parse_value(parser *ps);

static json_value *new_value(json_type t) {
    json_value *v = calloc(1, sizeof(json_value));
    v->type = t;
    return v;
}

static char *parse_raw_string(parser *ps) {
    /* assumes *ps->p == '"' */
    ps->p++;
    size_t cap = 32, len = 0;
    char *buf = malloc(cap);
    while (*ps->p && *ps->p != '"') {
        char c = *ps->p++;
        if (c == '\\') {
            char e = *ps->p++;
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    /* skip 4 hex digits, emit '?' (no full unicode support needed here) */
                    for (int i = 0; i < 4 && *ps->p; i++) ps->p++;
                    c = '?';
                    break;
                }
                default: c = e;
            }
        }
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = c;
    }
    if (*ps->p == '"') ps->p++;
    else { ps->err = "unterminated string"; }
    buf[len] = '\0';
    return buf;
}

static json_value *parse_string(parser *ps) {
    json_value *v = new_value(JSON_STRING);
    v->u.string = parse_raw_string(ps);
    return v;
}

static json_value *parse_number(parser *ps) {
    const char *start = ps->p;
    if (*ps->p == '-' || *ps->p == '+') ps->p++;
    while (isdigit((unsigned char)*ps->p)) ps->p++;
    if (*ps->p == '.') { ps->p++; while (isdigit((unsigned char)*ps->p)) ps->p++; }
    if (*ps->p == 'e' || *ps->p == 'E') {
        ps->p++;
        if (*ps->p == '-' || *ps->p == '+') ps->p++;
        while (isdigit((unsigned char)*ps->p)) ps->p++;
    }
    json_value *v = new_value(JSON_NUMBER);
    v->u.number = strtod(start, NULL);
    return v;
}

static json_value *parse_array(parser *ps) {
    json_value *v = new_value(JSON_ARRAY);
    ps->p++; /* [ */
    skip_ws(ps);
    size_t cap = 4;
    v->u.array.items = malloc(cap * sizeof(json_value *));
    v->u.array.count = 0;
    if (*ps->p == ']') { ps->p++; return v; }
    while (*ps->p) {
        skip_ws(ps);
        json_value *item = parse_value(ps);
        if (!item) { ps->err = "invalid array item"; return v; }
        if (v->u.array.count == cap) { cap *= 2; v->u.array.items = realloc(v->u.array.items, cap * sizeof(json_value *)); }
        v->u.array.items[v->u.array.count++] = item;
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; break; }
        ps->err = "expected ',' or ']' in array";
        break;
    }
    return v;
}

static json_value *parse_object(parser *ps) {
    json_value *v = new_value(JSON_OBJECT);
    ps->p++; /* { */
    skip_ws(ps);
    size_t cap = 4;
    v->u.object.keys = malloc(cap * sizeof(char *));
    v->u.object.values = malloc(cap * sizeof(json_value *));
    v->u.object.count = 0;
    if (*ps->p == '}') { ps->p++; return v; }
    while (*ps->p) {
        skip_ws(ps);
        if (*ps->p != '"') { ps->err = "expected string key"; break; }
        char *key = parse_raw_string(ps);
        skip_ws(ps);
        if (*ps->p != ':') { ps->err = "expected ':'"; free(key); break; }
        ps->p++;
        skip_ws(ps);
        json_value *val = parse_value(ps);
        if (!val) { ps->err = "invalid object value"; free(key); break; }
        if (v->u.object.count == cap) {
            cap *= 2;
            v->u.object.keys = realloc(v->u.object.keys, cap * sizeof(char *));
            v->u.object.values = realloc(v->u.object.values, cap * sizeof(json_value *));
        }
        v->u.object.keys[v->u.object.count] = key;
        v->u.object.values[v->u.object.count] = val;
        v->u.object.count++;
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; break; }
        ps->err = "expected ',' or '}' in object";
        break;
    }
    return v;
}

static json_value *parse_value(parser *ps) {
    skip_ws(ps);
    switch (*ps->p) {
        case '"': return parse_string(ps);
        case '{': return parse_object(ps);
        case '[': return parse_array(ps);
        case 't':
            if (strncmp(ps->p, "true", 4) == 0) { ps->p += 4; json_value *v = new_value(JSON_BOOL); v->u.boolean = 1; return v; }
            ps->err = "invalid token"; return NULL;
        case 'f':
            if (strncmp(ps->p, "false", 5) == 0) { ps->p += 5; json_value *v = new_value(JSON_BOOL); v->u.boolean = 0; return v; }
            ps->err = "invalid token"; return NULL;
        case 'n':
            if (strncmp(ps->p, "null", 4) == 0) { ps->p += 4; return new_value(JSON_NULL); }
            ps->err = "invalid token"; return NULL;
        default:
            if (*ps->p == '-' || isdigit((unsigned char)*ps->p)) return parse_number(ps);
            ps->err = "unexpected character";
            return NULL;
    }
}

json_value *json_parse(const char *text, const char **err) {
    parser ps = { text, NULL };
    json_value *v = parse_value(&ps);
    if (!ps.err && v) {
        skip_ws(&ps);
        if (*ps.p) ps.err = "trailing data after JSON value";
    }
    if (ps.err) {
        if (err) *err = ps.err;
        if (v) json_free(v);
        return NULL;
    }
    if (err) *err = NULL;
    return v;
}

void json_free(json_value *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->u.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++) json_free(v->u.array.items[i]);
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.keys[i]);
                json_free(v->u.object.values[i]);
            }
            free(v->u.object.keys);
            free(v->u.object.values);
            break;
        default:
            break;
    }
    free(v);
}

json_value *json_object_get(const json_value *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.keys[i], key) == 0) return obj->u.object.values[i];
    }
    return NULL;
}

const char *json_get_string(const json_value *obj, const char *key, const char *def) {
    json_value *v = json_object_get(obj, key);
    if (v && v->type == JSON_STRING) return v->u.string;
    return def;
}

double json_get_number(const json_value *obj, const char *key, double def) {
    json_value *v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) return v->u.number;
    return def;
}

const char **json_get_string_array(const json_value *obj, const char *key, size_t *count) {
    json_value *v = json_object_get(obj, key);
    if (!v || v->type != JSON_ARRAY) { if (count) *count = 0; return NULL; }
    const char **out = malloc(v->u.array.count * sizeof(char *));
    size_t n = 0;
    for (size_t i = 0; i < v->u.array.count; i++) {
        if (v->u.array.items[i]->type == JSON_STRING) out[n++] = v->u.array.items[i]->u.string;
    }
    if (count) *count = n;
    return out;
}

json_value *json_new_object(void) { return new_value(JSON_OBJECT); }
json_value *json_new_array(void) { return new_value(JSON_ARRAY); }

json_value *json_new_string(const char *s) {
    json_value *v = new_value(JSON_STRING);
    v->u.string = strdup(s ? s : "");
    if (!v->u.string) { free(v); return NULL; }
    return v;
}

json_value *json_new_number(double n) {
    json_value *v = new_value(JSON_NUMBER);
    v->u.number = n;
    return v;
}

int json_object_set(json_value *obj, const char *key, json_value *value) {
    if (!obj || obj->type != JSON_OBJECT || !key || !value) return -1;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.keys[i], key) == 0) {
            json_free(obj->u.object.values[i]);
            obj->u.object.values[i] = value;
            return 0;
        }
    }
    size_t n = obj->u.object.count;
    char *copy = strdup(key);
    char **keys = realloc(obj->u.object.keys, (n + 1) * sizeof(*keys));
    if (!copy || !keys) { free(copy); return -1; }
    obj->u.object.keys = keys;
    json_value **values = realloc(obj->u.object.values, (n + 1) * sizeof(*values));
    if (!values) { free(copy); return -1; }
    obj->u.object.values = values;
    obj->u.object.keys[n] = copy;
    obj->u.object.values[n] = value;
    obj->u.object.count++;
    return 0;
}

int json_array_append(json_value *array, json_value *value) {
    if (!array || array->type != JSON_ARRAY || !value) return -1;
    size_t n = array->u.array.count;
    json_value **items = realloc(array->u.array.items, (n + 1) * sizeof(*items));
    if (!items) return -1;
    array->u.array.items = items;
    array->u.array.items[n] = value;
    array->u.array.count++;
    return 0;
}

typedef struct { char *data; size_t len, cap; int failed; } json_buf;

static void json_buf_add(json_buf *b, const char *s, size_t n) {
    if (b->failed || n > SIZE_MAX - b->len - 1) { b->failed = 1; return; }
    size_t need = b->len + n + 1;
    if (need > b->cap) {
        size_t cap = b->cap ? b->cap : 128;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { b->failed = 1; return; }
            cap *= 2;
        }
        char *p = realloc(b->data, cap);
        if (!p) { b->failed = 1; return; }
        b->data = p;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void stringify_string(json_buf *b, const char *s) {
    json_buf_add(b, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char escaped[7];
        switch (*p) {
            case '"': json_buf_add(b, "\\\"", 2); break;
            case '\\': json_buf_add(b, "\\\\", 2); break;
            case '\b': json_buf_add(b, "\\b", 2); break;
            case '\f': json_buf_add(b, "\\f", 2); break;
            case '\n': json_buf_add(b, "\\n", 2); break;
            case '\r': json_buf_add(b, "\\r", 2); break;
            case '\t': json_buf_add(b, "\\t", 2); break;
            default:
                if (*p < 0x20) {
                    snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                    json_buf_add(b, escaped, 6);
                } else {
                    json_buf_add(b, (const char *)p, 1);
                }
        }
    }
    json_buf_add(b, "\"", 1);
}

static void stringify_value(json_buf *b, const json_value *v) {
    if (!v) { json_buf_add(b, "null", 4); return; }
    switch (v->type) {
        case JSON_NULL: json_buf_add(b, "null", 4); break;
        case JSON_BOOL: {
            const char *s = v->u.boolean ? "true" : "false";
            json_buf_add(b, s, strlen(s));
            break;
        }
        case JSON_NUMBER: {
            char n[64];
            int len = snprintf(n, sizeof(n), "%.17g", v->u.number);
            if (len < 0 || (size_t)len >= sizeof(n)) b->failed = 1;
            else json_buf_add(b, n, (size_t)len);
            break;
        }
        case JSON_STRING: stringify_string(b, v->u.string ? v->u.string : ""); break;
        case JSON_ARRAY:
            json_buf_add(b, "[", 1);
            for (size_t i = 0; i < v->u.array.count; i++) {
                if (i) json_buf_add(b, ",", 1);
                stringify_value(b, v->u.array.items[i]);
            }
            json_buf_add(b, "]", 1);
            break;
        case JSON_OBJECT:
            json_buf_add(b, "{", 1);
            for (size_t i = 0; i < v->u.object.count; i++) {
                if (i) json_buf_add(b, ",", 1);
                stringify_string(b, v->u.object.keys[i]);
                json_buf_add(b, ":", 1);
                stringify_value(b, v->u.object.values[i]);
            }
            json_buf_add(b, "}", 1);
            break;
    }
}

char *json_stringify(const json_value *v) {
    json_buf b = {0};
    stringify_value(&b, v);
    if (b.failed) { free(b.data); return NULL; }
    if (!b.data) return strdup("null");
    return b.data;
}
