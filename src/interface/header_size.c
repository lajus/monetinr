#define USE_RINTERNALS
#include "Rinternals.h"
#include "stdio.h"

int main() {
	printf("%zu\n", sizeof(SEXPREC_ALIGN));
	return sizeof(SEXPREC_ALIGN);
}
