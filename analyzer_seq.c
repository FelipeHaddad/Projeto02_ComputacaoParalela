#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hash_table.h"

#define HASH_TABLE_SIZE 131071UL
#define MAX_LINE_LEN 8192
#define MAX_URL_LEN 2048

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

static int process_log_seq(HashTable *ht, const char *log_path) {
    FILE *fp;
    char line[MAX_LINE_LEN];
    char url[MAX_URL_LEN];

    if (!ht || !log_path) {
        return 0;
    }

    fp = fopen(log_path, "r");
    if (!fp) {
        fprintf(stderr, "Erro ao abrir log '%s': %s\n", log_path, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), fp)) {
        CacheNode *node;

        if (!extract_url(line, url, sizeof(url))) {
            continue;
        }

        node = ht_get(ht, url);
        if (node) {
            node->hit_count++;
        }
    }

    fclose(fp);
    return 1;
}

int main(int argc, char **argv) {
    const char *log_path = "log_distribuido.txt";
    const char *manifest_path = "manifest.txt";
    const char *output_path = "results.csv";
    HashTable *ht;

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

    if (!process_log_seq(ht, log_path)) {
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    ht_save_results(ht, output_path);
    ht_destroy(ht);
    return EXIT_SUCCESS;
}
