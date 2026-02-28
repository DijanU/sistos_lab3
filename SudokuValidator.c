/*
 * SudokuValidator.c
 * CC3064 Sistemas Operativos - Laboratorio 03
 * Universidad del Valle de Guatemala
 *
 * Compilar:
 *   gcc -o SudokuValidator SudokuValidator.c -lpthread -fopenmp
 *
 * Ejecutar:
 *   ./SudokuValidator sudoku
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <pthread.h>
#include <omp.h>

/* ─── Arreglo global 9×9 ─────────────────────────────────────────── */
int grid[9][9];

/* Resultado global de validación */
int sudoku_valid = 1;

/* ─── Verificar columna ───────────────────────────────────────────── */
int checkColumn(int col) {
    int seen[10] = {0};
    for (int i = 0; i < 9; i++) {
        int val = grid[i][col];
        if (val < 1 || val > 9 || seen[val]) return 0;
        seen[val] = 1;
    }
    return 1;
}

/* ─── Verificar fila ─────────────────────────────────────────────── */
int checkRow(int row) {
    int seen[10] = {0};
    for (int j = 0; j < 9; j++) {
        int val = grid[row][j];
        if (val < 1 || val > 9 || seen[val]) return 0;
        seen[val] = 1;
    }
    return 1;
}

/* ─── Verificar subgrid 3×3 ──────────────────────────────────────── */
int checkSubgrid(int startRow, int startCol) {
    int seen[10] = {0};
    for (int i = startRow; i < startRow + 3; i++) {
        for (int j = startCol; j < startCol + 3; j++) {
            int val = grid[i][j];
            if (val < 1 || val > 9 || seen[val]) return 0;
            seen[val] = 1;
        }
    }
    return 1;
}

/* ─── Función del pthread (revisión de columnas) ─────────────────── */
void* checkColumnsThread(void* arg) {
    omp_set_num_threads(9); /* un thread por columna */

    pid_t tid = syscall(SYS_gettid);
    printf("El thread que ejecuta el metodo de revision de columnas es: %d\n", tid);

    int local_valid = 1;

    #pragma omp parallel for schedule(dynamic) private(tid)
    for (int col = 0; col < 9; col++) {
        tid = syscall(SYS_gettid);
        printf("En la revision de columnas el siguiente es un thread en ejecucion: %d\n", tid);
        if (!checkColumn(col)) {
            #pragma omp critical
            local_valid = 0;
        }
    }

    if (!local_valid) sudoku_valid = 0;

    pthread_exit(0);
}

/* ─── Función de revisión de filas (con OpenMP) ──────────────────── */
void reviewRows() {
    omp_set_num_threads(9);
    omp_set_nested(1); /* habilitar paralelismo anidado */

    int local_valid = 1;

    #pragma omp parallel for schedule(dynamic) private(local_valid)
    for (int row = 0; row < 9; row++) {
        if (!checkRow(row)) {
            #pragma omp critical
            sudoku_valid = 0;
        }
    }
}

/* ─── Ejecutar ps sobre un PID dado ──────────────────────────────── */
void run_ps(pid_t target_pid) {
    char pid_str[20];
    snprintf(pid_str, sizeof(pid_str), "%d", target_pid);
    execlp("ps", "ps", "-p", pid_str, "-lLf", NULL);
    /* execlp no retorna si tiene éxito */
    perror("execlp");
    exit(1);
}

/* ═══════════════════════════════════════════════════════════════════ */
int main(int argc, char* argv[]) {

    /* Paso de configuración: limitar threads de OpenMP en main */
    /* Comentar/descomentar según el paso del laboratorio:      */
    /* omp_set_num_threads(1); */

    /* ── 1. Validar argumentos ─────────────────────────────── */
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <archivo_sudoku>\n", argv[0]);
        return 1;
    }

    /* ── 2. Abrir y mapear archivo con mmap ────────────────── */
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) { perror("open"); return 1; }

    struct stat sb;
    if (fstat(fd, &sb) == -1) { perror("fstat"); return 1; }

    char* file_data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);

    /* ── 3. Copiar dígitos al arreglo global grid[9][9] ───── */
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 9; j++) {
            grid[i][j] = file_data[i * 9 + j] - '0';
        }
    }
    munmap(file_data, sb.st_size);

    /* ── 4. Revisar subgrids 3×3 (posiciones [0,3,6]) ─────── */
    omp_set_num_threads(3);
    omp_set_nested(1);

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < 9; i += 3) {
        if (!checkSubgrid(i, i)) {
            #pragma omp critical
            sudoku_valid = 0;
        }
    }

    /* ── 5. Obtener PID del proceso padre ───────────────────── */
    pid_t parent_pid = getpid();

    /* ── 6. Fork #1: ejecutar ps durante revisión de columnas ─ */
    pid_t fork1 = fork();
    if (fork1 == 0) {
        /* Proceso hijo #1 */
        run_ps(parent_pid);
    }

    /* ── Proceso padre: crear pthread para columnas ──────────── */
    pthread_t col_thread;
    pthread_create(&col_thread, NULL, checkColumnsThread, NULL);
    pthread_join(col_thread, NULL);

    /* Mostrar TID del thread principal (main) */
    pid_t main_tid = syscall(SYS_gettid);
    printf("El thread en el que se ejecuta main es: %d\n", main_tid);

    /* Esperar al hijo que ejecutó ps */
    waitpid(fork1, NULL, 0);

    /* ── Revisar filas ────────────────────────────────────────── */
    reviewRows();

    /* ── Desplegar resultado ──────────────────────────────────── */
    if (sudoku_valid)
        printf("Sudoku resuelto!\n");
    else
        printf("Sudoku invalido.\n");

    /* ── 7. Fork #2: ps antes de terminar ─────────────────────── */
    printf("Antes de terminar el estado de este proceso y sus threads es:\n");
    pid_t fork2 = fork();
    if (fork2 == 0) {
        run_ps(parent_pid);
    }

    waitpid(fork2, NULL, 0);
    return 0;
}
