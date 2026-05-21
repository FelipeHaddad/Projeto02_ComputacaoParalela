#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hash_table.h"

// Tamanho sugerido para a tabela (número primo próximo a uma potência de 2)
#define HASH_SIZE 131071
// Tamanho máximo do buffer para ler cada linha dos arquivos
#define MAX_LINE_LEN 8192

int main(int argc, char* argv[]) {
    // Ve se o usuário passou pelo menos o arquivo de log como argumento
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_de_log> [manifest.txt] [results.csv]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Define os caminhos dos arquivos usando os argumentos ou valores padrão
    const char *log_path = argv[1];
    const char *manifest_path = (argc >= 3) ? argv[2] : "manifest.txt";
    const char *output_path = (argc >= 4) ? argv[3] : "results.csv";

    /* Construir Hash Table (Manifest) */
    // Aloca a memória inicial para a tabela hash
    HashTable* ht = ht_create(HASH_SIZE);
    
    // Abre o arquivo do manifesto que contém a lista de URLs válidas
    FILE* fp_manifest = fopen(manifest_path, "r");
    if (!fp_manifest) { 
        perror("Erro ao abrir manifest"); 
        return EXIT_FAILURE; 
    }

    char line[MAX_LINE_LEN];
    // Lê o manifesto linha por linha
    while (fgets(line, sizeof(line), fp_manifest)) {
        // Remove quebras de linha (\n ou \r) do final da string
        line[strcspn(line, "\r\n")] = '\0';
        
        // Se a linha não estiver vazia, insere a URL na tabela hash com hit_count = 0
        if (line[0] != '\0') {
            ht_put(ht, line);
        }
    }
    fclose(fp_manifest); // Fecha o manifesto após popular a tabela

    /* Processamento do Log */
    // Inicia a contagem do tempo de processamento do log
    clock_t inicio = clock();
    
    // Abre o arquivo de log para leitura
    FILE* fp_log = fopen(log_path, "r");
    if (!fp_log) { 
        perror("Erro ao abrir log"); 
        ht_destroy(ht); 
        return EXIT_FAILURE; 
    }

    char url[2048];
    // Lê cada linha do arquivo de log
    while (fgets(line, sizeof(line), fp_log)) {
        // Ignora os primeiros campos do log (IP, data, etc.) e extrai apenas a URL da requisição HTTP
        if (sscanf(line, "%*s %*s %*s %*s %*s \"%*s %2047s", url) == 1) {
            // Busca o nó correspondente à URL na tabela hash
            CacheNode* node = ht_get(ht, url);
            
            // Se encontrar incrementa o contador de acessos
            if (node) {
                node->hit_count++;
            }
        }
    }
    fclose(fp_log); // Fecha o arquivo de log

    // Finaliza a contagem do tempo
    clock_t fim = clock();

    /* Resultados */
    // Grava todas as URLs e seus respectivos contadores no arquivo CSV de saída
    ht_save_results(ht, output_path);
    
    // Calcula o tempo total gasto na Fase 2 em segundos
    double elapsed = (double)(fim - inicio) / CLOCKS_PER_SEC;
    printf("Tempo de processamento: %.3f segundos\n", elapsed);

    // Libera toda a memória alocada para a tabela e seus nós
    ht_destroy(ht);
    
    return EXIT_SUCCESS;
}