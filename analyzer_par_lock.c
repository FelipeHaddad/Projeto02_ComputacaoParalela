#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINE_LEN 8192

// Função auxiliar para calcular o índice (reproduzida aqui pois a original é static)
size_t calcular_hash(const char* str, size_t size) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash % size;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *log_path = argv[1];
    HashTable* ht = ht_create(HASH_SIZE);

    // --- CRIAÇÃO DOS LOCKS (Gerenciados aqui na main) ---
    omp_lock_t *locks = malloc(sizeof(omp_lock_t) * HASH_SIZE);
    for(int i = 0; i < HASH_SIZE; i++) omp_init_lock(&locks[i]);

    // --- FASE 1: Construção da Tabela (Sequencial) ---
    FILE* fp_manifest = fopen("manifest.txt", "r");
    if (!fp_manifest) { perror("Erro ao abrir manifest"); return EXIT_FAILURE; }
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') ht_put(ht, line);
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
    
    // Tratamento de erro do fread para evitar avisos
    if (bytes_lidos != (size_t)fsize) {
        fprintf(stderr, "Erro ao ler o log completo.\n");
        free(buffer);
        fclose(fp_log);
        ht_destroy(ht);
        for(int i = 0; i < HASH_SIZE; i++) omp_destroy_lock(&locks[i]);
        free(locks);
        return EXIT_FAILURE;
    }
    
    buffer[fsize] = '\0';
    fclose(fp_log);

    // Criar array de ponteiros para as linhas
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

    // --- FASE 3: Processamento Paralelo com Lock Granular ---
    #pragma omp parallel for
    for (int i = 0; i < num_lines; i++) {
        char url[2048];
        if (sscanf(lines[i], "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
            CacheNode* node = ht_get(ht, url);
            
            if (node) {
                // Calcula o índice para travar apenas o bucket correto
                size_t index = calcular_hash(url, HASH_SIZE);
                
                // Trava o acesso apenas a este bucket específico
                omp_set_lock(&locks[index]);
                node->hit_count++;
                omp_unset_lock(&locks[index]);
            }
        }
    }

    // --- FASE 4: Resultados ---
    ht_save_results(ht, "results.csv");
    ht_destroy(ht);
    
    // Liberação de memória
    for(int i = 0; i < HASH_SIZE; i++) omp_destroy_lock(&locks[i]);
    free(locks);
    free(buffer);
    free(lines);

    return EXIT_SUCCESS;
}