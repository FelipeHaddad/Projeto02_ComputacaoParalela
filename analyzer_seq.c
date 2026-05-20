#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINE_LEN 8192

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log> [manifest.txt] [results.csv]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *log_path = argv[1];
    const char *manifest_path = (argc >= 3) ? argv[2] : "manifest.txt";
    const char *output_path = (argc >= 4) ? argv[3] : "results.csv";

    /* FASE 1: Construir Hash Table (Manifest) */
    clock_t inicio = clock();
    HashTable* ht = ht_create(HASH_SIZE);
    FILE* fp_manifest = fopen(manifest_path, "r");
    if (!fp_manifest) { perror("Erro ao abrir manifest"); return EXIT_FAILURE; }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') ht_put(ht, line);
    }
    fclose(fp_manifest);

    /* FASE 2: Processamento do Log  */

    FILE* fp_log = fopen(log_path, "r");
    if (!fp_log) { perror("Erro ao abrir log"); ht_destroy(ht); return EXIT_FAILURE; }

    char url[2048];
    while (fgets(line, sizeof(line), fp_log)) {
        if (sscanf(line, "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
            CacheNode* node = ht_get(ht, url);
            if (node) node->hit_count++;
        }
    }
    fclose(fp_log);

    clock_t fim = clock();
    /* FIM DA MEDIÇÃO */

    /* FASE 3: Resultados */
    ht_save_results(ht, output_path);
    
    double elapsed = (double)(fim - inicio) / CLOCKS_PER_SEC;
    printf("Tempo de processamento: %.3f segundos\n", elapsed);

    ht_destroy(ht);
    return EXIT_SUCCESS;
}
