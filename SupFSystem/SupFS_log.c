//
// Created by romain on 26/05/16.
//

#include "SupFS_log.h"

int log_error(char *name){
    int returnValue = -errno;
    printf("Error %s, ",name);
    return returnValue;
}