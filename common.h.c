#include "common.h"
#include <stdlib.h>

int generate_task(Task *task) {
    char ops[] = {'+', '-', '*', '/'};
    task->operator = ops[rand() % 4];
    task->operand1 = rand() % 100;
    task->operand2 = rand() % 100;
    if (task->operator == '/') {
        while (task->operand2 == 0)
            task->operand2 = rand() % 100;
    }
    return 0;
}

int calculate_task(const Task *task) {
    switch (task->operator) {
        case '+': return task->operand1 + task->operand2;
        case '-': return task->operand1 - task->operand2;
        case '*': return task->operand1 * task->operand2;
        case '/': return task->operand1 / task->operand2;
    }
    return 0; // fallback
}
