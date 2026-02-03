// C3 FFI Demo - Calling C functions from C3
module main;

import std::io;
import ffi;

fn void main() {
    io::printn("=== C3 FFI Demo ===\n");

    // Basic math operations
    io::printn("-- Basic Math --");
    int sum = ffi::add(10, 25);
    io::printfn("add(10, 25) = %d", sum);

    int product = ffi::multiply(7, 8);
    io::printfn("multiply(7, 8) = %d", product);

    double result = ffi::power(2.0, 10);
    io::printfn("power(2.0, 10) = %.0f", result);

    // Array operations
    io::printn("\n-- Array Operations --");
    int[5] numbers = { 1, 2, 3, 4, 5 };
    int arr_sum = ffi::sum_array(&numbers[0], 5);
    io::printfn("sum_array([1,2,3,4,5]) = %d", arr_sum);

    ffi::scale_array(&numbers[0], 5, 10);
    io::print("scale_array by 10: [");
    for (int i = 0; i < 5; i++) {
        if (i > 0) io::print(", ");
        io::printf("%d", numbers[i]);
    }
    io::printn("]");

    // String operations
    io::printn("\n-- String Operations --");
    ZString hello = "Hello, C3!";
    int len = ffi::string_length(hello);
    io::printfn("string_length(\"Hello, C3!\") = %d", len);

    // Mutable buffer for string reversal
    char[10] buf = {'C','3',' ','r','o','c','k','s','!',0};
    io::printfn("Before reverse: \"%s\"", (ZString)&buf);
    ffi::string_reverse(&buf[0]);
    io::printfn("After reverse:  \"%s\"", (ZString)&buf);

    // Point/struct operations
    io::printn("\n-- Struct Operations --");
    ffi::Point p1 = { .x = 0.0, .y = 0.0 };
    ffi::Point p2 = { .x = 3.0, .y = 4.0 };

    double dist = ffi::point_distance(&p1, &p2);
    io::printfn("Distance from (0,0) to (3,4) = %.2f", dist);

    ffi::Point mid = ffi::point_midpoint(&p1, &p2);
    io::printfn("Midpoint: (%.2f, %.2f)", mid.x, mid.y);

    io::printn("\n=== Demo Complete ===");
}
