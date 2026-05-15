CC = gcc
CFLAGS = -O2 -fopenmp

TARGETS = analyzer_seq \
	analyzer_par_atomic \
	analyzer_par_critical \
	analyzer_par_lock

all: $(TARGETS)

analyzer_seq: analyzer_seq.c hash_table.c hash_table.h
	$(CC) $(CFLAGS) analyzer_seq.c hash_table.c -o analyzer_seq

analyzer_par_atomic: analyzer_par_atomic.c hash_table.c hash_table.h
	$(CC) $(CFLAGS) analyzer_par_atomic.c hash_table.c -o analyzer_par_atomic

analyzer_par_critical: analyzer_par_critical.c hash_table.c hash_table.h
	$(CC) $(CFLAGS) analyzer_par_critical.c hash_table.c -o analyzer_par_critical

analyzer_par_lock: analyzer_par_lock.c hash_table.c hash_table.h
	$(CC) $(CFLAGS) analyzer_par_lock.c hash_table.c -o analyzer_par_lock

clean:
	rm -f $(TARGETS) $(TARGETS:=.exe)
