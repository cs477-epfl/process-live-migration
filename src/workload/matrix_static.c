#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define MATRIX_SIZE 10     // Define a large matrix size
#define MODULO 1000          // Define the modulo value to keep elements within [0, MODULO - 1]

// Function to initialize a matrix with random values
void initialize_matrix_random(uint32_t matrix[MATRIX_SIZE][MATRIX_SIZE]) {
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            // Initialize with random values from 0 to MODULO - 1
            matrix[i][j] = rand() % MODULO;
        }
    }
}

// Function to multiply two matrices using modular arithmetic
void multiply_matrices_modulo(uint32_t A[MATRIX_SIZE][MATRIX_SIZE], uint32_t B[MATRIX_SIZE][MATRIX_SIZE], uint32_t result[MATRIX_SIZE][MATRIX_SIZE], uint32_t modulo) {
    // Initialize result matrix to zero
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int j = 0; j < MATRIX_SIZE; j++) {
            result[i][j] = 0;
        }
    }

    // Perform multiplication using modular arithmetic
    for (int i = 0; i < MATRIX_SIZE; i++) {
        for (int k = 0; k < MATRIX_SIZE; k++) {
            uint32_t a_ik = A[i][k];
            for (int j = 0; j < MATRIX_SIZE; j++) {
                uint64_t temp = (uint64_t)a_ik * B[k][j] + result[i][j];
                result[i][j] = (uint32_t)(temp % modulo);
            }
        }
    }
}

// Function to partially print a matrix
void print_partial_matrix(uint32_t matrix[MATRIX_SIZE][MATRIX_SIZE], int iteration) {
    printf("Iteration %d: Printing a few elements of the matrix:\n", iteration);
    for (int i = 0; i < 5; i++) { // Print first 5 rows
        for (int j = 0; j < 5; j++) { // Print first 5 columns
            printf("%8u ", matrix[i][j]);
        }
        printf("\n");
    }
    printf("...\n\n");
}

int main() {
    srand(time(NULL)); // Seed the random number generator

    // Declare matrices as static arrays
    static uint32_t A_prev[MATRIX_SIZE][MATRIX_SIZE];
    static uint32_t A_curr[MATRIX_SIZE][MATRIX_SIZE];
    static uint32_t A_next[MATRIX_SIZE][MATRIX_SIZE];

    // Initialize the first two matrices with random values
    initialize_matrix_random(A_prev);
    initialize_matrix_random(A_curr);

    printf("Initial matrices generated.\n");

    int iteration = 1;

    // Perform iterative matrix multiplication indefinitely
    while (1) {
        printf("Iteration %d: Multiplying matrices...\n", iteration);

        // Multiply A_curr and A_prev, store the result in A_next
        multiply_matrices_modulo(A_curr, A_prev, A_next, MODULO);

        // Print partial result
        print_partial_matrix(A_next, iteration);

        // Prepare for next iteration
        // Swap matrices: A_prev becomes old A_curr, A_curr becomes A_next
        for (int i = 0; i < MATRIX_SIZE; i++) {
            for (int j = 0; j < MATRIX_SIZE; j++) {
                A_prev[i][j] = A_curr[i][j];
                A_curr[i][j] = A_next[i][j];
            }
        }

        // Increment iteration counter
        iteration++;
    }

    return 0;
}
