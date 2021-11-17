#ifndef __CONFIG_H
#define __CONFIG_H

#include <stdbool.h>

#define LOG_FILE_MAX 100000
#define LOG_FILE "logfile.log"
#define VERBOSE_MODE true

const unsigned int maxTimeBetweenNewProcsSecs = 0;
const unsigned int minTimeBetweenNewProcsSecs = 0;
const unsigned int minTimeBetweenNewProcsNS = 1000000; // 1 ms
const unsigned int maxTimeBetweenNewProcsNS = 500000000; // 500 ms

#endif
