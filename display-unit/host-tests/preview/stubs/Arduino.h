#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
#ifdef __cplusplus
}
#include <algorithm>
using std::min; using std::max;
bool getLocalTime(struct tm *t, uint32_t ms = 5000);
#endif
