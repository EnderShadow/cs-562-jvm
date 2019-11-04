//
// Created by matthew on 10/3/19.
//

#ifndef JVM_UTILS_H
#define JVM_UTILS_H

#include "dataTypes.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define KIBIBYTES(x) ((x) * 1024)
#define MEBIBYTES(x) (KIBIBYTES(x) * 1024)
#define GIBIBYTES(x) (MEBIBYTES(x) * 1024)

#define KILOBYTES(x) ((x) * 1000)
#define MEGABYTES(x) (KILOBYTES(x) * 1000)
#define GIGABYTES(x) (MEGABYTES(x) * 1000)

#define ALIGN(x) (((x) + 7) & ~7)

slot_t convertToJavaString(char *arg);

#endif //JVM_UTILS_H
