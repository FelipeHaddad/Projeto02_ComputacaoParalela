#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINE_LEN 8192
#define MAX_LINES 10000000  // 10 milhões de linhas

/**
 * Estrutura para armazenar as linhas do log em memória
 */
typedef struct {
    char** linhas;
    size_t num_linhas;
} LogBuffer;

/**
 * Carrega o log inteiro em memória para melhor paralelização
 */
LogBuffer* load_log_to_memory(const char* log_path) {
    LogBuffer* lb = (LogBuffer*)malloc(sizeof(LogBuffer));
    if (!lb) {
        perror("Erro ao alocar LogBuffer");
        return NULL;
    }

    FILE* fp = fopen(log_path, "r");
    if (!fp) {
        perror("Erro ao abrir arquivo de log");
        free(lb);
        return NULL;
    }

    // Aloca espaço para ponteiros das linhas
    lb->linhas = (char**)malloc(sizeof(char*) * MAX_LINES);
    if (!lb->linhas) {
        perror("Erro ao alocar array de linhas");
        fclose(fp);
        free(lb);
        return NULL;
    }

    lb->num_linhas = 0;
    char line[MAX_LINE_LEN];

    // Lê as linhas e armazena em memória
    while (fgets(line, sizeof(line), fp) && lb->num_linhas < MAX_LINES) {
        size_t len = strlen(line) + 1;
        lb->linhas[lb->num_linhas] = (char*)malloc(len);
        
        if (!lb->linhas[lb->num_linhas]) {
            perror("Erro ao alocar linha");
            fclose(fp);
            return lb;  // Retorna parcialmente carregado
        }

        strcpy(lb->linhas[lb->num_linhas], line);
        lb->num_linhas++;
    }

    fclose(fp);
    return lb;
}

/**
 * Libera o buffer do log
 */
void free_log_buffer(LogBuffer* lb) {
    if (!lb) return;
    for (size_t i = 0; i < lb->num_linhas; i++) {
        free(lb->linhas[i]);
    }
    free(lb->linhas);
    free(lb);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log> [manifest.txt] [results.csv]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *log_path = argv[1];
    const char *manifest_path = (argc >= 3) ? argv[2] : "manifest.txt";
    const char *output_path = (argc >= 4) ? argv[3] : "results.csv";



    /* FASE 1: Construir Hash Table (Manifest) */
    HashTable* ht = ht_create(HASH_SIZE);
    if (!ht) {
        fprintf(stderr, "Erro ao criar hash table\n");
        return EXIT_FAILURE;
    }

    FILE* fp_manifest = fopen(manifest_path, "r");
    if (!fp_manifest) {
        perror("Erro ao abrir manifest");
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    char line[MAX_LINE_LEN];
    size_t manifest_count = 0;
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') {
            ht_put(ht, line);
            manifest_count++;
        }
    }
    fclose(fp_manifest);

    /* FASE 2: Carregar log em memória */
    LogBuffer* log_buffer = load_log_to_memory(log_path);
    if (!log_buffer) {
        fprintf(stderr, "Erro ao carregar log\n");
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    /* FASE 3: Processamento do Log em PARALELO com CRITICAL */
    clock_t inicio = clock();

    #pragma omp parallel
    {
        char url[2048];
        
        #pragma omp for
        for (size_t i = 0; i < log_buffer->num_linhas; i++) {
            // Extrai a URL da linha
            if (sscanf(log_buffer->linhas[i], "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
                CacheNode* node = ht_get(ht, url);
                
                if (node) {
                    // REGIÃO CRÍTICA: Atualiza o contador
                    #pragma omp critical
                    {
                        node->hit_count++;
                    }
                }
            }
        }
    }

    clock_t fim = clock();
    double elapsed = (double)(fim - inicio) / CLOCKS_PER_SEC;
    /* FIM DA MEDIÇÃO */

    /* FASE 4: Salvar resultados */
    ht_save_results(ht, output_path);

    /* Estatísticas finais */
    printf("Tempo de processamento: %.3f segundos\n", elapsed);


    /* Limpeza */
    free_log_buffer(log_buffer);
    ht_destroy(ht);

    return EXIT_SUCCESS;
}
