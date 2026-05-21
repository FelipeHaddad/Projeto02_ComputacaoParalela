#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINE_LEN 8192
#define MAX_LINES 10000000

// Estrutura que vai guardar os ponteiros das linhas lidas
typedef struct {
    char** linhas;
    size_t num_linhas;
} LogBuffer;

// Transfere o arquivo de log do disco para a memória RAM
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

    // Cria a tabela de ponteiros para armazenar cada linha individualmente
    lb->linhas = (char**)malloc(sizeof(char*) * MAX_LINES);
    if (!lb->linhas) {
        perror("Erro ao alocar array de linhas");
        fclose(fp);
        free(lb);
        return NULL;
    }

    lb->num_linhas = 0;
    char line[MAX_LINE_LEN];

    // Varre o arquivo alocando o tamanho exato de cada string na memória
    while (fgets(line, sizeof(line), fp) && lb->num_linhas < MAX_LINES) {
        size_t len = strlen(line) + 1;
        lb->linhas[lb->num_linhas] = (char*)malloc(len);
        
        if (!lb->linhas[lb->num_linhas]) {
            perror("Erro ao alocar linha");
            fclose(fp);
            return lb;
        }

        strcpy(lb->linhas[lb->num_linhas], line);
        lb->num_linhas++;
    }

    fclose(fp);
    return lb;
}

// Desaloca todas as strings e estruturas do buffer de log
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

    // Inicializa a tabela hash vazia para o manifesto
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
    
    // Alimenta a tabela hash sequencialmente com as URLs mapeadas no manifesto
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') {
            ht_put(ht, line);
            manifest_count++;
        }
    }
    fclose(fp_manifest);

    // Invoca a função que joga o log para a memória RAM antes do processamento paralelo
    LogBuffer* log_buffer = load_log_to_memory(log_path);
    if (!log_buffer) {
        fprintf(stderr, "Erro ao carregar log\n");
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    // Início do cronômetro
    clock_t inicio = clock();

    // Cria a equipe de threads concorrentes do OpenMP
    #pragma omp parallel
    {
        char url[2048];
        
        // Distribui de forma automática as linhas do buffer entre as threads
        #pragma omp for
        for (size_t i = 0; i < log_buffer->num_linhas; i++) {
            // Faz a filtragem e extração da URL presente na linha atual
            if (sscanf(log_buffer->linhas[i], "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
                CacheNode* node = ht_get(ht, url);
                
                if (node) {
                    // Sincronização por exclusão mútua global
                    // Força com que apenas uma thread por vez execute este trecho de código, independente de qual seja o nó
                    #pragma omp critical
                    {
                        node->hit_count++;
                    }
                }
            }
        }
    }

    // Fim da área paralelizada e parada do cronômetro
    clock_t fim = clock();
    double elapsed = (double)(fim - inicio) / CLOCKS_PER_SEC;

    // Exporta os dados consolidados da tabela para o arquivo CSV de saída
    ht_save_results(ht, output_path);

    printf("Tempo de processamento: %.3f segundos\n", elapsed);

    // Faz a limpeza completa de toda a memória dinâmica que foi utilizada
    free_log_buffer(log_buffer);
    ht_destroy(ht);

    return EXIT_SUCCESS;
}