// FFI Bindings for C mathutils library
module ffi;

// Struct definition matching C's Point
struct Point {
    double x;
    double y;
}

// Basic math operations
extern fn int add(int a, int b);
extern fn int multiply(int a, int b);
extern fn double power(double base, int exp);

// Array operations
extern fn int sum_array(int* arr, int len);
extern fn void scale_array(int* arr, int len, int factor);

// String utilities
extern fn int string_length(char* str);
extern fn void string_reverse(char* str);

// Point operations
extern fn double point_distance(Point* a, Point* b);
extern fn Point point_midpoint(Point* a, Point* b);
