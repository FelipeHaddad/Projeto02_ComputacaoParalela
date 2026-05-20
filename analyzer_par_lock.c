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
 * Função de Hash (djb2) - mesmo algoritmo de hash_table.c
 * Converte uma string (URL) em um índice para a tabela.
 */
static size_t hash_djb2(const char* str, size_t size) {
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    return hash % size;
}

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

    /* FASE 1.5: Inicializar locks para cada bucket */
    omp_lock_t* locks = (omp_lock_t*)malloc(sizeof(omp_lock_t) * HASH_SIZE);
    if (!locks) {
        perror("Erro ao alocar array de locks");
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < HASH_SIZE; i++) {
        omp_init_lock(&locks[i]);
    }

    /* FASE 2: Carregar log em memória */
    LogBuffer* log_buffer = load_log_to_memory(log_path);
    if (!log_buffer) {
        fprintf(stderr, "Erro ao carregar log\n");
        
        // Limpeza dos locks antes de sair
        for (size_t i = 0; i < HASH_SIZE; i++) {
            omp_destroy_lock(&locks[i]);
        }
        free(locks);
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    /* FASE 3: Processamento do Log em PARALELO com BUCKET LOCKS */
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
                    // BUCKET LOCK: Sincronização granular por bucket
                    // 1. Calcula o índice (bucket) baseado na URL
                    size_t bucket = hash_djb2(url, HASH_SIZE);
                    
                    // 2. Adquire o lock do bucket
                    omp_set_lock(&locks[bucket]);
                    
                    // 3. Atualiza o contador (seção crítica do bucket)
                    node->hit_count++;
                    
                    // 4. Libera o lock do bucket
                    omp_unset_lock(&locks[bucket]);
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
    
    // Destruir todos os locks
    for (size_t i = 0; i < HASH_SIZE; i++) {
        omp_destroy_lock(&locks[i]);
    }
    free(locks);
    
    ht_destroy(ht);

    return EXIT_SUCCESS;
}
