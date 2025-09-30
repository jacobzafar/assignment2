#ifndef COMMON_H
#define COMMON_H

#include <sys/socket.h>

#define MAX_BUFFER_SIZE 1024

typedef struct {
    int operand1;
    int operand2;
    char operator; // '+', '-', '*', '/'
} Task;

int generate_task(Task *task);
int calculate_task(const Task *task);

#endif // COMMON_H