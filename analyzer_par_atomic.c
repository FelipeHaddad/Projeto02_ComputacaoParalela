#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINE_LEN 8192
#define MAX_URL_LEN 2048

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log> [manifest.txt] [results.csv]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *log_path = argv[1];
    const char *manifest_path = (argc >= 3) ? argv[2] : "manifest.txt";
    const char *output_path = (argc >= 4) ? argv[3] : "results.csv";

    clock_t inicio = clock();

    /* FASE 1: Hash Table */
    HashTable* ht = ht_create(HASH_SIZE);
    FILE* fp_manifest = fopen(manifest_path, "r");
    if (!fp_manifest) { perror("Erro manifest"); return EXIT_FAILURE; }
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') ht_put(ht, line);
    }
    fclose(fp_manifest);

    /* FASE 2: Leitura com realocação dinâmica (Segurança contra Segfault) */
    FILE* fp_log = fopen(log_path, "r");
    if (!fp_log) { perror("Erro log"); ht_destroy(ht); return EXIT_FAILURE; }

    size_t cap = 10000; 
    size_t count = 0;
    char** url_list = malloc(sizeof(char*) * cap);

    char url_temp[MAX_URL_LEN];
    while (fgets(line, sizeof(line), fp_log)) {
        if (sscanf(line, "%*s %*s %*s %*s %*s \"%*s %2047s", url_temp) == 1) {
            if (count >= cap) {
                cap *= 2;
                url_list = realloc(url_list, sizeof(char*) * cap);
            }
            url_list[count++] = strdup(url_temp);
        }
    }
    fclose(fp_log);

    /* FASE 3: Processamento com verificação de ponteiro NULL */
    #pragma omp parallel for
    for (size_t i = 0; i < count; i++) {
        if (url_list[i] == NULL) continue;
        
        CacheNode* node = ht_get(ht, url_list[i]);
        if (node != NULL) {
            #pragma omp atomic update
            node->hit_count++;
        }
    }

    clock_t fim = clock();

    ht_save_results(ht, output_path);
    printf("Tempo total: %.3f segundos\n", (double)(fim - inicio) / CLOCKS_PER_SEC);

    /* Limpeza */
    for (size_t i = 0; i < count; i++) free(url_list[i]);
    free(url_list);
    ht_destroy(ht);
    return EXIT_SUCCESS;
}
