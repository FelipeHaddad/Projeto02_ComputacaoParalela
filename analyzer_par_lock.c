#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINE_LEN 8192
#define MAX_LINES 10000000

// Define o container que vai manter todas as linhas do log salvas na RAM
typedef struct {
    char** linhas;
    size_t num_linhas;
} LogBuffer;

// Transforma o conteúdo textual da string (URL) em um índice numérico válido para o array de locks
static size_t hash_djb2(const char* str, size_t size) {
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % size;
}

// Aloca memória e carrega o arquivo de log para o buffer em memória estruturado
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

    lb->linhas = (char**)malloc(sizeof(char*) * MAX_LINES);
    if (!lb->linhas) {
        perror("Erro ao alocar array de linhas");
        fclose(fp);
        free(lb);
        return NULL;
    }

    lb->num_linhas = 0;
    char line[MAX_LINE_LEN];

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

// Varre o buffer liberando o espaço de cada linha alocada dinamicamente
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

    // Aloca a estrutura interna principal da tabela hash
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
    
    // Varre o arquivo de manifesto preenchendo a tabela hash de forma serial
    while (fgets(line, sizeof(line), fp_manifest)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') {
            ht_put(ht, line);
            manifest_count++;
        }
    }
    fclose(fp_manifest);

    // Aloca um array de travas independentes onde cada índice protege um bucket específico
    omp_lock_t* locks = (omp_lock_t*)malloc(sizeof(omp_lock_t) * HASH_SIZE);
    if (!locks) {
        perror("Erro ao alocar array de locks");
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    // Inicializa cada um dos locks criados no array para deixá-los prontos para uso
    for (size_t i = 0; i < HASH_SIZE; i++) {
        omp_init_lock(&locks[i]);
    }

    // Dispara a leitura completa do log em disco direto para a memória RAM
    LogBuffer* log_buffer = load_log_to_memory(log_path);
    if (!log_buffer) {
        fprintf(stderr, "Erro ao carregar log\n");
        
        // Garante a desmontagem correta dos locks caso o programa falhe ao carregar o log
        for (size_t i = 0; i < HASH_SIZE; i++) {
            omp_destroy_lock(&locks[i]);
        }
        free(locks);
        ht_destroy(ht);
        return EXIT_FAILURE;
    }

    // Dispara o cronômetro
    clock_t inicio = clock();

    // Cria a região paralela que invoca múltiplas threads ao mesmo tempo
    #pragma omp parallel
    {
        char url[2048];
        
        // Reparte de forma paralela as iterações do laço de repetição entre os núcleos
        #pragma omp for
        for (size_t i = 0; i < log_buffer->num_linhas; i++) {
            // Analisa o texto do log para filtrar o campo específico da URL solicitado
            if (sscanf(log_buffer->linhas[i], "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
                CacheNode* node = ht_get(ht, url);
                
                if (node) {
                    // Identifica qual bucket guarda essa URL específica
                    size_t bucket = hash_djb2(url, HASH_SIZE);
                    
                    // Bloqueia temporariamente apenas o bucket correspondente à URL atual
                    omp_set_lock(&locks[bucket]);
                    
                    // Incrementa de forma segura o contador de acessos do nó
                    node->hit_count++;
                    
                    // Libera imediatamente a trava do bucket para outras threads acessarem
                    omp_unset_lock(&locks[bucket]);
                }
            }
        }
    }

    // Interrompe o cronômetro
    clock_t fim = clock();
    double elapsed = (double)(fim - inicio) / CLOCKS_PER_SEC;

    // Salva todo o mapeamento consolidado de dados no arquivo final em formato CSV
    ht_save_results(ht, output_path);

    printf("Tempo de processamento: %.3f segundos\n", elapsed);

    // Desaloca o buffer de linhas do log
    free_log_buffer(log_buffer);
    
    // Percorre destruindo e liberando a memória ocupada pelo sistema de travas
    for (size_t i = 0; i < HASH_SIZE; i++) {
        omp_destroy_lock(&locks[i]);
    }
    free(locks);
    
    // Libera a tabela hash inteira
    ht_destroy(ht);

    return EXIT_SUCCESS;
}