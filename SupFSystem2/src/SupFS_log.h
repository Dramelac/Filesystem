//
// Created by romain on 26/05/16.
//


#ifndef SUPFSYSTEM_SUPFS_LOG_H
#define SUPFSYSTEM_SUPFS_LOG_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int log_error(char *name);
void log_info(char *msg);

#endif //SUPFSYSTEM_SUPFS_LOG_H
