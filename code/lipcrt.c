#include "inc/lipon.h"
#include <stdio.h>

#define U64_STR_FMT "%lu"

extern void ldump(lipon_CState* state) {
	printf("ldump: " U64_STR_FMT "\n", LIPON_POP(state));
}

