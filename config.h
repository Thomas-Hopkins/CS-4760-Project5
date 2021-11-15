#ifndef __CONFIG_H
#define __CONFIG_H

#define LOG_FILE_MAX 10000
#define LOG_FILE "logfile.log"

const unsigned int maxTimeBetweenNewProcsNS = 0;
const unsigned int maxTimeBetweenNewProcsSecs = 1;

const unsigned int percentChanceIsIO = 10;
const unsigned int percentChanceTerminate = 10;
const unsigned int percentChanceFinish = 100;
const unsigned int percentChanceBlock = 20;

#endif
