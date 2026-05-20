#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h> // Necessário para OpenMP [cite: 312]
#include "hash_table.h"

#define HASH_SIZE 131071 // Tamanho sugerido [cite: 137]
#define MAX_LINE_LEN 8192

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *log_path = argv[1];
    HashTable* ht = ht_create(HASH_SIZE);

    // --- FASE 1: Construção da Tabela (Sequencial) ---
    clock_t inicio = clock();
    FILE* fp_manifest = fopen("manifest.txt", "r");
    if (!fp_manifest) { perror("Erro ao abrir manifest"); return EXIT_FAILURE; }
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') ht_put(ht, line); // Chamada corrigida para ht_put 
    }
    fclose(fp_manifest);

    // --- FASE 2: Carregamento dos Logs em memória ---
    FILE* fp_log = fopen(log_path, "r");
    if (!fp_log) { perror("Erro ao abrir log"); ht_destroy(ht); return EXIT_FAILURE; }
    
    fseek(fp_log, 0, SEEK_END);
    long fsize = ftell(fp_log);
    rewind(fp_log);

    char *buffer = malloc(fsize + 1);
    size_t bytes_lidos = fread(buffer, 1, fsize, fp_log);
    if (bytes_lidos != (size_t)fsize) {
        fprintf(stderr, "Erro ao ler o log completo.\n");
        free(buffer);
        fclose(fp_log);
        ht_destroy(ht);
        return EXIT_FAILURE;
    }
    buffer[fsize] = '\0';
    fclose(fp_log);

    // Criar array de ponteiros para as linhas para permitir processamento paralelo [cite: 163]
    int num_lines = 0;
    for(int i = 0; i < fsize; i++) if(buffer[i] == '\n') num_lines++;
    char **lines = malloc(num_lines * sizeof(char*));
    
    int idx = 0;
    lines[idx++] = buffer;
    for(int i = 0; i < fsize; i++) {
        if(buffer[i] == '\n') {
            buffer[i] = '\0';
            if(idx < num_lines) lines[idx++] = &buffer[i+1];
        }
    }

    // --- FASE 3: Processamento Paralelo ---
    #pragma omp parallel for // Paralelização do loop [cite: 164]
    for (int i = 0; i < num_lines; i++) {
        char url[2048];
        if (sscanf(lines[i], "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
            CacheNode* node = ht_get(ht, url);
            
            // Região crítica: sincronização de granularidade grossa [cite: 182]
            if (node) {
                #pragma omp critical
                {
                    node->hit_count++;
                }
            }
        }
    }
    clock_t fim = clock();
     double elapsed = (double)(fim - inicio) / CLOCKS_PER_SEC;
    printf("Tempo de processamento: %.3f segundos\n", elapsed);
    // --- FASE 4: Resultados ---
    ht_save_results(ht, "results.csv"); // Salva resultados [cite: 120]
    ht_destroy(ht);
    free(buffer);
    free(lines);

    return EXIT_SUCCESS;
}