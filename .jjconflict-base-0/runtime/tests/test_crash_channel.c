#include <stdio.h>
#include <stdlib.h>
#include "../include/omni.h"

int main() {
    printf("Testing unbuffered channel (capacity 0) crash...\n");
    Obj* ch = make_channel(0);
    printf("Created channel with capacity 0.\n");
    
    // This should trigger % 0 in runtime.c:2905 (approximately)
    printf("Attempting to send to unbuffered channel (this should crash)...\n");
    channel_send(ch, mk_int(42));
    
    printf("If you see this, it didn't crash (which is unexpected if capacity is 0).\n");
    return 0;
}
