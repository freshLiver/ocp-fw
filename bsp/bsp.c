#include "bsp.h"
#include "debug.h"

char inbyte()
{
    pr_info("Waiting for keyboard input: ");
    return getc(stdin);
}
void *void_func() { return NULL; }