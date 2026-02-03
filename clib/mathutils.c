#include "mathutils.h"
#include <math.h>
#include <string.h>

int32_t add(int32_t a, int32_t b) {
    return a + b;
}

int32_t multiply(int32_t a, int32_t b) {
    return a * b;
}

double power(double base, int32_t exp) {
    return pow(base, (double)exp);
}

int32_t sum_array(int32_t* arr, int32_t len) {
    int32_t sum = 0;
    for (int32_t i = 0; i < len; i++) {
        sum += arr[i];
    }
    return sum;
}

void scale_array(int32_t* arr, int32_t len, int32_t factor) {
    for (int32_t i = 0; i < len; i++) {
        arr[i] *= factor;
    }
}

int32_t string_length(const char* str) {
    return (int32_t)strlen(str);
}

void string_reverse(char* str) {
    int32_t len = strlen(str);
    for (int32_t i = 0; i < len / 2; i++) {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}

double point_distance(Point* a, Point* b) {
    double dx = b->x - a->x;
    double dy = b->y - a->y;
    return sqrt(dx * dx + dy * dy);
}

Point point_midpoint(Point* a, Point* b) {
    Point mid;
    mid.x = (a->x + b->x) / 2.0;
    mid.y = (a->y + b->y) / 2.0;
    return mid;
}
