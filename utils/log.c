#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include "utils.h"

//Outra coisa que eu sabia que existia, mas nunca tinha usado...
//Não entendi da onde vem o log_level... '-'
void log_if_level(int level, const char *format, ...) {
    if (log_level > level) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}   
