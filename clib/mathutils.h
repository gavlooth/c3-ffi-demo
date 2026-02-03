#ifndef MATHUTILS_H
#define MATHUTILS_H

#include <stdint.h>

// Basic math operations
int32_t add(int32_t a, int32_t b);
int32_t multiply(int32_t a, int32_t b);
double power(double base, int32_t exp);

// Array operations
int32_t sum_array(int32_t* arr, int32_t len);
void scale_array(int32_t* arr, int32_t len, int32_t factor);

// String utilities
int32_t string_length(const char* str);
void string_reverse(char* str);

// Struct example
typedef struct {
    double x;
    double y;
} Point;

double point_distance(Point* a, Point* b);
Point point_midpoint(Point* a, Point* b);

#endif
