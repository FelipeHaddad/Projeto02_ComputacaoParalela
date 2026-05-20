#include <errno.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hash_table.h"

#define HASH_TABLE_SIZE 131071UL
#define MAX_LINE_LEN 8192
#define MAX_URL_LEN 2048
#define INITIAL_URL_CAPACITY 1024

// Função para limpar quebras de linha
static void trim_newline(char *s) {
    size_t n = strcspn(s, "\r\n");
    s[n] = '\0';
}

// Função para extrair a URL
static int extract_url(const char *line, char *out_url, size_t out_size) {
    const char *quote_start = strchr(line, '\"');
    if (!quote_start) return 0;
    quote_start++;
    while (*quote_start == ' ') quote_start++;

    const char *method_end = strchr(quote_start, ' ');
    if (!method_end) return 0;

    size_t len = method_end - quote_start;
    if (len >= out_size) len = out_size - 1;
    
    strncpy(out_url, quote_start, len);
    out_url[len] = '\0';
    return 1;
}

// Funções de carregamento (Manifest e Log) iguais às anteriores...
int load_manifest(HashTable *ht, const char *manifest_path) {
    FILE *fp = fopen(manifest_path, "r");
    if (!fp) return 0;
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if (line[0] != '\0') ht_put(ht, line);
    }
    fclose(fp);
    return 1;
}

int load_log_urls(const char *log_path, char ***urls_out, size_t *count_out) {
    FILE *fp = fopen(log_path, "r");
    if (!fp) return 0;
    size_t capacity = INITIAL_URL_CAPACITY;
    size_t count = 0;
    char **urls = malloc(sizeof(char *) * capacity);
    char line[MAX_LINE_LEN];
    char url[MAX_URL_LEN];
    while (fgets(line, sizeof(line), fp)) {
        if (extract_url(line, url, sizeof(url))) {
            if (count >= capacity) {
                capacity *= 2;
                urls = realloc(urls, sizeof(char *) * capacity);
            }
            urls[count++] = strdup(url);
        }
    }
    fclose(fp);
    *urls_out = urls;
    *count_out = count;
    return 1;
}

// --- FASE 2: Processamento com Bucket Lock ---
static void process_urls_parallel_lock(HashTable *ht, char **urls, size_t count, omp_lock_t *locks) {
    size_t i;
    // Paraleliza o loop
    #pragma omp parallel for default(none) shared(ht, urls, count, locks) private(i) schedule(static)
    for (i = 0; i < count; i++) {
        // Encontra o bucket correto para a URL
        size_t bucket = hash_djb2(urls[i], ht->size); 

        // Trava apenas o bucket específico (Granularidade Fina)
        omp_set_lock(&locks[bucket]);
        
        CacheNode *node = ht_get(ht, urls[i]);
        if (node) {
            node->hit_count++;
        }
        
        omp_unset_lock(&locks[bucket]);
    }
}

int main(int argc, char **argv) {
    const char *log_path = (argc >= 2) ? argv[1] : "log_distribuido.txt";
    const char *manifest_path = (argc >= 3) ? argv[2] : "manifest.txt";
    const char *output_path = (argc >= 4) ? argv[3] : "results.csv";

    HashTable *ht = ht_create(HASH_TABLE_SIZE);
    load_manifest(ht, manifest_path);

    // Inicialização das travas (uma para cada bucket)
    omp_lock_t *locks = malloc(sizeof(omp_lock_t) * HASH_TABLE_SIZE);
    for (size_t i = 0; i < HASH_TABLE_SIZE; i++) omp_init_lock(&locks[i]);

    char **urls = NULL;
    size_t url_count = 0;

    clock_t inicio = clock();

    load_log_urls(log_path, &urls, &url_count);
    process_urls_parallel_lock(ht, urls, url_count, locks);

    clock_t fim = clock();

    ht_save_results(ht, output_path);
    printf("Tempo total (Bucket Lock): %.3f segundos\n", (double)(fim - inicio) / CLOCKS_PER_SEC);

    // Limpeza
    for (size_t i = 0; i < HASH_TABLE_SIZE; i++) omp_destroy_lock(&locks[i]);
    free(locks);
    for (size_t i = 0; i < url_count; i++) free(urls[i]);
    free(urls);
    ht_destroy(ht);
    return 0;
}
