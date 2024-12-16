#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MATRIX_SIZE 10      // Define a large matrix size
#define BITMASK 0xFFFFFFFF        // 16-bit mask to limit elements to 16 bits (0 to 65535)

// Function to allocate a matrix
uint32_t** allocate_matrix(int size) {
    uint32_t** matrix = malloc(size * sizeof(uint32_t*));
    if (matrix == NULL) {
        fprintf(stderr, "Error allocating memory for matrix rows.\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < size; i++) {
        matrix[i] = malloc(size * sizeof(uint32_t));
        if (matrix[i] == NULL) {
            fprintf(stderr, "Error allocating memory for matrix columns.\n");
            exit(EXIT_FAILURE);
        }
    }
    return matrix;
}

// Function to deallocate a matrix
void deallocate_matrix(uint32_t** matrix, int size) {
    for (int i = 0; i < size; i++) {
        free(matrix[i]);
    }
    free(matrix);
}

// Function to initialize a matrix with deterministic values
void initialize_matrix(uint32_t** matrix, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            // Initialize with values from 1 to 10
            matrix[i][j] = rand() & BITMASK;
        }
    }
}

// Function to multiply two matrices using bitwise AND to limit values
void multiply_matrices_bitmask(uint32_t** A, uint32_t** B, uint32_t** result, int size, uint32_t bitmask) {
    // Initialize result matrix to zero
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            result[i][j] = 0;
        }
    }

    // Perform multiplication
    for (int i = 0; i < size; i++) {
        for (int k = 0; k < size; k++) {
            uint32_t a_ik = A[i][k];
            for (int j = 0; j < size; j++) {
                uint64_t temp = (uint64_t)a_ik * B[k][j] + result[i][j]; // Use uint64_t to prevent overflow
                result[i][j] = (uint32_t)(temp & bitmask);               // Apply bitmask
            }
        }
    }
}

// Function to partially print a matrix
void print_partial_matrix(uint32_t** matrix, int size, int iteration) {
    printf("Iteration %d: Printing a few elements of the matrix:\n", iteration);
    for (int i = 0; i < size && i < 5; i++) {         // Print first 5 rows
        for (int j = 0; j < size && j < 5; j++) {     // Print first 5 columns
            printf("%8u ", matrix[i][j]);
        }
        printf("\n");
    }
    printf("...\n\n");
}

int main() {
    // Allocate matrices
    uint32_t** A_prev = allocate_matrix(MATRIX_SIZE);
    uint32_t** A_curr = allocate_matrix(MATRIX_SIZE);
    uint32_t** A_next = allocate_matrix(MATRIX_SIZE);

    // Initialize the first two matrices with deterministic values
    initialize_matrix(A_prev, MATRIX_SIZE);
    initialize_matrix(A_curr, MATRIX_SIZE);

    printf("Initial matrices generated.\n");

    int iteration = 1;

    // Perform iterative matrix multiplication indefinitely
    while (1) {
        printf("Iteration %d: Multiplying matrices...\n", iteration);

        // Multiply A_curr and A_prev using bitmask, store the result in A_next
        multiply_matrices_bitmask(A_curr, A_prev, A_next, MATRIX_SIZE, BITMASK);

        // Print partial result
        print_partial_matrix(A_next, MATRIX_SIZE, iteration);

        // Prepare for next iteration
        // Swap pointers so A_prev becomes the old A_curr, A_curr becomes A_next
        uint32_t** temp = A_prev;
        A_prev = A_curr;
        A_curr = A_next;
        A_next = temp;  // Reuse the matrix for the next result

        // Increment iteration counter
        iteration++;
    }

    // The program will not reach this point due to the infinite loop
    // Deallocate matrices (unreachable code)
    deallocate_matrix(A_prev, MATRIX_SIZE);
    deallocate_matrix(A_curr, MATRIX_SIZE);
    deallocate_matrix(A_next, MATRIX_SIZE);

    return 0;
}
