#include <errno.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hash_table.h"

#define HASH_TABLE_SIZE 131071UL
#define MAX_LINE_LEN 8192
#define MAX_URL_LEN 2048
#define INITIAL_URL_CAPACITY 1024

static void trim_newline(char *s) {
    size_t n;
    if (!s) {
        return;
    }
    n = strcspn(s, "\r\n");
    s[n] = '\0';
}

static int extract_url(const char *line, char *out_url, size_t out_size) {
    const char *quote_start;
    const char *method_end;
    const char *url_start;
    const char *url_end;
    size_t len;

    if (!line || !out_url || out_size == 0) {
        return 0;
    }

    quote_start = strchr(line, '\"');
    if (!quote_start) {
        return 0;
    }
    quote_start++;

    while (*quote_start == ' ') {
        quote_start++;
    }

    method_end = strchr(quote_start, ' ');
    if (!method_end) {
        return 0;
    }

    url_start = method_end + 1;
    while (*url_start == ' ') {
        url_start++;
    }

    url_end = strchr(url_start, ' ');
    if (!url_end || url_end <= url_start) {
        return 0;
    }

    len = (size_t)(url_end - url_start);
    if (len >= out_size) {
        return 0;
    }

    memcpy(out_url, url_start, len);
    out_url[len] = '\0';
    return 1;
}

static int load_manifest(HashTable *ht, const char *manifest_path) {
    FILE *fp;
    char line[MAX_LINE_LEN];

    if (!ht || !manifest_path) {
        return 0;
    }

    fp = fopen(manifest_path, "r");
    if (!fp) {
        fprintf(stderr, "Erro ao abrir manifest '%s': %s\n", manifest_path, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }
        ht_put(ht, line);
    }

    fclose(fp);
    return 1;
}

static int push_url(char ***urls, size_t *count, size_t *capacity, const char *url) {
    char *url_copy;
    char **new_urls;

    if (!urls || !count || !capacity || !url) {
        return 0;
    }

    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? INITIAL_URL_CAPACITY : (*capacity * 2);
        new_urls = (char **)realloc(*urls, new_capacity * sizeof(char *));
        if (!new_urls) {
            return 0;
        }
        *urls = new_urls;
        *capacity = new_capacity;
    }

    url_copy = (char *)malloc(strlen(url) + 1);
    if (!url_copy) {
        return 0;
    }
    strcpy(url_copy, url);

    (*urls)[*count] = url_copy;
    (*count)++;
    return 1;
}

static void free_urls(char **urls, size_t count) {
    size_t i;
    if (!urls) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(urls[i]);
    }
    free(urls);
}

static int load_log_urls(const char *log_path, char ***urls_out, size_t *count_out) {
    FILE *fp;
    char line[MAX_LINE_LEN];
    char url[MAX_URL_LEN];
    char **urls = NULL;
    size_t count = 0;
    size_t capacity = 0;

    if (!log_path || !urls_out || !count_out) {
        return 0;
    }

    fp = fopen(log_path, "r");
    if (!fp) {
        fprintf(stderr, "Erro ao abrir log '%s': %s\n", log_path, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (!extract_url(line, url, sizeof(url))) {
            continue;
        }

        if (!push_url(&urls, &count, &capacity, url)) {
            fprintf(stderr, "Erro de memória ao carregar URLs do log.\n");
            free_urls(urls, count);
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    *urls_out = urls;
    *count_out = count;
    return 1;
}

static void process_urls_parallel_atomic(HashTable *ht, char **urls, size_t count) {
    size_t i;

    if (!ht || !urls) {
        return;
    }

#pragma omp parallel for default(none) shared(ht, urls, count) private(i) schedule(static)
    for (i = 0; i < count; i++) {
        CacheNode *node = ht_get(ht, urls[i]);
        if (!node) {
            continue;
        }
#pragma omp atomic update
        node->hit_count++;
    }
}

int main(int argc, char **argv) {
    const char *log_path = "log_distribuido.txt";
    const char *manifest_path = "manifest.txt";
    const char *output_path = "results.csv";
    HashTable *ht;
    char **urls = NULL;
    size_t url_count = 0;

    if (argc >= 2) {
        log_path = argv[1];
    }
    if (argc >= 3) {
        manifest_path = argv[2];
    }
    if (argc >= 4) {
        output_path = argv[3];
    }

    ht = ht_create(HASH_TABLE_SIZE);
    if (!ht) {
        fprintf(stderr, "Falha ao criar hash table.\n");
        return EXIT_FAILURE;
    }

    if (!load_manifest(ht, manifest_path)) {
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    if (!load_log_urls(log_path, &urls, &url_count)) {
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    process_urls_parallel_atomic(ht, urls, url_count);
    ht_save_results(ht, output_path);

    free_urls(urls, url_count);
    ht_destroy(ht);
    return EXIT_SUCCESS;
}
