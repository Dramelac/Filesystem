//
// Created by romain on 26/05/16.
//

#include "SupFS_log.h"

int log_error(char *name){
    int returnValue = -errno;
    char cmd[100] = "echo \"ERROR : ";
    strcat(cmd, name);
    strcat(cmd, " !\" >> log.txt");
    system(cmd);
    return returnValue;
}

void log_info(char *msg, const char* add){
    char cmd[100] = "echo \"INFO : ";
    strcat(cmd, msg);
    strcat(cmd, add);
    strcat(cmd, "\" >> log.txt");
    system(cmd);
}

