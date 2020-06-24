#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

extern int num_CPUs(void); // number of logical CPUs

