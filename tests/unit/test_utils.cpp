#include "test_utils.h"
#include <unistd.h>

void sleep_x_secs(void *x) { sleep(*((int *)x)); }
