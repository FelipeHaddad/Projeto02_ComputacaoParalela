#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINE_LEN 8192
#define MAX_LINES 10000000  // Limite máximo de 10 milhões de linhas para o log

// Estrutura para armazenar as linhas do log em memória
typedef struct {
    char** linhas;       // Array de strings (ponteiros para caracteres)
    size_t num_linhas;   // Quantidade de linhas efetivamente carregadas
} LogBuffer;

// Carrega o log inteiro em memória para melhor paralelização
LogBuffer* load_log_to_memory(const char* log_path) {
    // Aloca a estrutura do buffer
    LogBuffer* lb = (LogBuffer*)malloc(sizeof(LogBuffer));
    if (!lb) {
        perror("Erro ao alocar LogBuffer");
        return NULL;
    }

    // Abre o arquivo de log para leitura
    FILE* fp = fopen(log_path, "r");
    if (!fp) {
        perror("Erro ao abrir arquivo de log");
        free(lb);
        return NULL;
    }

    // Aloca espaço para armazenar os ponteiros de cada linha
    lb->linhas = (char**)malloc(sizeof(char*) * MAX_LINES);
    if (!lb->linhas) {
        perror("Erro ao alocar array de linhas");
        fclose(fp);
        free(lb);
        return NULL;
    }

    lb->num_linhas = 0;
    char line[MAX_LINE_LEN];

    // Lê as linhas do arquivo e as copia para a memória até o fim ou atingir o limite
    while (fgets(line, sizeof(line), fp) && lb->num_linhas < MAX_LINES) {
        size_t len = strlen(line) + 1;
        lb->linhas[lb->num_linhas] = (char*)malloc(len); // Aloca espaço exato para a linha atual
        
        if (!lb->linhas[lb->num_linhas]) {
            perror("Erro ao alocar linha");
            fclose(fp);
            return lb;  // Retorna o que já foi carregado parcialmente em caso de falha
        }

        strcpy(lb->linhas[lb->num_linhas], line); // Copia o conteúdo da linha lida
        lb->num_linhas++;
    }

    fclose(fp); // Fecha o arquivo de log
    return lb;
}

// Libera a memória ocupada pelo buffer do log
void free_log_buffer(LogBuffer* lb) {
    if (!lb) return;
    // Libera cada linha individualmente
    for (size_t i = 0; i < lb->num_linhas; i++) {
        free(lb->linhas[i]);
    }
    free(lb->linhas); // Libera o array de ponteiros
    free(lb);         // Libera a estrutura principal
}

int main(int argc, char* argv[]) {
    // Validação dos argumentos de linha de comando
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log> [manifest.txt] [results.csv]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *log_path = argv[1];
    const char *manifest_path = (argc >= 3) ? argv[2] : "manifest.txt";
    const char *output_path = (argc >= 4) ? argv[3] : "results.csv";

    /* Construir Hash Table (Manifest) */
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
    // Carrega o arquivo de manifesto e popula a tabela de forma sequencial
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') {
            ht_put(ht, line);
            manifest_count++;
        }
    }
    fclose(fp_manifest);

    /* Carregar log em memória */
    // Transfere o arquivo de log para a RAM para evitar que a leitura de disco seja um gargalo para as threads
    LogBuffer* log_buffer = load_log_to_memory(log_path);
    if (!log_buffer) {
        fprintf(stderr, "Erro ao carregar log\n");
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    /* Processamento do Log em Paralelo */
    clock_t inicio = clock(); // Inicia a medição do tempo

    // Cria a região paralela do OpenMP
    #pragma omp parallel
    {
        // Variável local privada para cada thread armazenar a URL extraída temporariamente
        char url[2048];
        
        // Divide as iterações do loop 'for' entre as threads disponíveis
        #pragma omp for
        for (size_t i = 0; i < log_buffer->num_linhas; i++) {
            // Cada thread processa uma linha do buffer e extrai a URL
            if (sscanf(log_buffer->linhas[i], "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
                CacheNode* node = ht_get(ht, url); // Busca a URL na estrutura de dados compartilhada
                
                if (node) {
                    // Sincronização por Hardware: Protege apenas a operação de escrita
                    // Impede que duas threads incrementem o mesmo nó simultaneamente
                    #pragma omp atomic update
                    node->hit_count++;
                }
            }
        }
    }

    clock_t fim = clock(); // Finaliza a medição do tempo
    double elapsed = (double)(fim - inicio) / CLOCKS_PER_SEC;

    /* Salva os resultados */
    // Grava os contadores gerados no arquivo final
    ht_save_results(ht, output_path);

    /* Estatísticas finais */
    printf("Tempo de processamento: %.3f segundos\n", elapsed);

    /* Limpeza */
    // Desaloca toda a memória utilizada
    free_log_buffer(log_buffer);
    ht_destroy(ht);

    return EXIT_SUCCESS;
}