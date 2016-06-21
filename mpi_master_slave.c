#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mpi.h"

#define TOTAL_ARRAYS  10000
#define TOTAL_NUMBERS 100000
#define MAX_NUMBER    TOTAL_ARRAYS * TOTAL_NUMBERS

#define MASTER 0
#define TAG_DIE TOTAL_ARRAYS + 1

#define T_NUMBER int
#define T_MPI_TYPE MPI_INT
#define PAYLOAD_SIZE 8

int myrank;

void master();
void slave();
void master_send_job(T_NUMBER **numbers, int job_index, int dest);
int master_receive_result();
void slave_receive_job();
void slave_send_result();
T_NUMBER** alloc_contiguous_matrix(int rows, int columns);

void debug_all_numbers(T_NUMBER **numbers);
void debug_numbers(T_NUMBER *numbers);
void my_log(char *fmt, ...);

int cmpfunc(const void * a, const void * b);

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  if (myrank == MASTER) {
    master();
  } else {
    slave();
  }
  MPI_Finalize();
  return 0;
}

void master() {
  int ntasks, rank, i, n;

  printf("Preparing arrays...\n");
  T_NUMBER** numbers = alloc_contiguous_matrix(TOTAL_ARRAYS, TOTAL_NUMBERS);
  for (i = 0; i < TOTAL_ARRAYS; i++) {
    for (n = 0; n < TOTAL_NUMBERS; n++)
      numbers[i][n] = MAX_NUMBER - i * TOTAL_NUMBERS - n;
  }
  printf("DONE\n");

  debug_all_numbers(numbers);

  MPI_Comm_size(MPI_COMM_WORLD, &ntasks);

  my_log("Seeding slaves");
  for (rank = 1; rank < ntasks; ++rank) {
    master_send_job(numbers, (rank - 1)*PAYLOAD_SIZE, rank);
  }

  my_log("Sending remaining jobs");
  for (i = ntasks-1; i < TOTAL_ARRAYS/PAYLOAD_SIZE; i++) {
    int source = master_receive_result(numbers);
    master_send_job(numbers, i*PAYLOAD_SIZE, source);
  }

  my_log("Done sending jobs, waiting to be completed");

  for (rank = 1; rank < ntasks; ++rank)
    master_receive_result(numbers);

  my_log("Killing slaves");
  for (rank = 1; rank < ntasks; ++rank) {
    MPI_Send(&rank, 1, T_MPI_TYPE, rank, TAG_DIE, MPI_COMM_WORLD);
  }
  my_log("DONE");

  debug_all_numbers(numbers);
}

void master_send_job(T_NUMBER **numbers, int job_index, int dest) {
  MPI_Send(&(numbers[job_index][0]), TOTAL_NUMBERS*PAYLOAD_SIZE, T_MPI_TYPE, dest, job_index, MPI_COMM_WORLD);
}

int master_receive_result(T_NUMBER **numbers) {
  MPI_Status status;

  MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
  int job_index = status.MPI_TAG;
  int source = status.MPI_SOURCE;

  MPI_Recv(&(numbers[job_index][0]), TOTAL_NUMBERS*PAYLOAD_SIZE, T_MPI_TYPE, source, job_index, MPI_COMM_WORLD, &status);

  return source;
}

void slave() {
  MPI_Status status;
  int job_index, i;

  T_NUMBER** payload = alloc_contiguous_matrix(PAYLOAD_SIZE, TOTAL_NUMBERS);
  if (payload == NULL) { fprintf(stderr, "calloc failed\n"); return; }

  omp_set_num_threads(8);

  for (;;) {
    MPI_Probe(MASTER, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
    if (status.MPI_TAG == TAG_DIE) { break; }

    job_index = status.MPI_TAG;

    MPI_Recv(&(payload[0][0]), TOTAL_NUMBERS*PAYLOAD_SIZE, T_MPI_TYPE, MASTER, job_index, MPI_COMM_WORLD, &status);
    #pragma omp for
    for (i = 0; i < PAYLOAD_SIZE; i++)
      qsort(payload[i], TOTAL_NUMBERS, sizeof(T_NUMBER), cmpfunc);

    MPI_Send(&(payload[0][0]), TOTAL_NUMBERS*PAYLOAD_SIZE, T_MPI_TYPE, MASTER, job_index, MPI_COMM_WORLD);
  }
}

int cmpfunc (const void * a, const void * b) {
  return ( *(T_NUMBER*)a - *(T_NUMBER*)b );
}

T_NUMBER** alloc_contiguous_matrix(int rows, int columns) {
  int i;

  T_NUMBER* data = calloc(rows*columns, sizeof(T_NUMBER));
  if (data == NULL) { fprintf(stderr, "calloc failed\n"); return NULL; }

  T_NUMBER** matrix = calloc(rows, sizeof(T_NUMBER *));
  if (matrix == NULL) { fprintf(stderr, "calloc failed\n"); return NULL; }

  for (i = 0; i < rows; i++)
    matrix[i] = &(data[i*columns]);

  return matrix;
}

void debug_all_numbers(T_NUMBER **numbers) {
  int i;
  printf("First 5 arrays:\n");
  for (i = 0; i < 5; i++) {
    debug_numbers(numbers[i]);
  }
  printf(" ...\n");
  for (i = TOTAL_ARRAYS - 5; i < TOTAL_ARRAYS; i++) {
    debug_numbers(numbers[i]);
  }
}

void debug_numbers(T_NUMBER* numbers) {
  int n;
  printf("[%d] [ ", myrank);
  for (n = 0; n < 3; n++) {
    printf("%07d ", numbers[n]);
  }
  printf(" ... ");
  for (n = TOTAL_NUMBERS - 3; n < TOTAL_NUMBERS; n++) {
    printf("%07d ", numbers[n]);
  }
  printf("]\n");
}

void my_log(char *fmt, ...) {
  va_list printfargs;
  printf("[%d] ", myrank);

  va_start(printfargs, fmt);
  vprintf(fmt, printfargs);
  va_end(printfargs);

  printf("\n");
}
