# Analisador de Desempenho de Cache de CDN com OpenMP

Este projeto consiste no desenvolvimento de um analisador paralelo simplificado capaz de processar milhões de requisições de logs de acesso e identificar padrões de acesso em tempo real. O foco é simular o monitoramento de infraestruturas de Content Delivery Networks (CDNs), onde o volume massivo de dados exige processamento paralelo para reduzir latência e custos operacionais.

## Objetivos do Projeto
O laboratório explora o desempenho de software em relação à arquitetura computacional, focando em:  
- Sincronização: Implementação de soluções utilizando OpenMP
- Análise de Concorrência: Investigação de condições de corrida, contenção e serialização.
- Arquitetura de Memória: Análise do impacto de false sharing e degradação de cache.
- Métricas de Hardware: Medição de IPC (Instructions Per Cycle), cache misses e speedup.

## Estratégias de Sincronização
Para contabilizar os acessos às URLs em uma tabela hash de forma segura e eficiente, são implementadas quatro versões:
1. Sequencial (analyzer_seq): Baseline de thread única para garantir a corretude.
2. OpenMP Critical (analyzer_par_critical): Sincronização de granularidade grossa através de #pragma omp critical.
3. OpenMP Atomic (analyzer_par_atomic): Utiliza instruções atômicas de hardware (#pragma omp atomic update).
4. OpenMP Bucket Lock (analyzer_par_lock): Sincronização de granularidade média utilizando locks independentes para cada bucket da tabela hash, visando reduzir a contenção e aumentar o paralelismo.

## Metodologia e Experimentos
O processamento é realizado em duas fases:
- Fase 1: Construção sequencial da tabela hash a partir do arquivo manifest.txt.
- Fase 2: Processamento paralelo dos logs (log_distribuido.txt e log_concorrente.txt) para incremento dos contadores de acesso.

A análise de performance utiliza ferramentas como perf (contadores reais de hardware) e Cachegrind (simulação de cache) para validar o impacto das diferentes granularidades de lock e do uso de padding para evitar false sharing.  

## Estrutura do Projeto
A organização segue um modelo modular:
- analyzer_*.c: Código-fonte das diferentes versões do analisador.
- hash_table.c / .h: Implementação da estrutura de dados
- Makefile: Automação da compilação com as flags -O2 e -fopenmp (obrigatório).
- manifest.txt / log_*.txt: Datasets para os experimentos.

## Como Executar
Para compilar todas as versões
```bash
make
```
Para executar a versão com atomic update usando 8 threads:
```bash
export OMP_NUM_THREADS=8
./analyzer_par_atomic log_distribuido.txt