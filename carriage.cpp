#include <stdio.h>
#include <iostream>
#include <unistd.h>

int main() {
    printf("My progress bar\n");
    for (int i = 0; i <= 100; i++) {
        // printf("\rProgress: %3d%%", i);

        std::cout << "\rProgress: " << i;

        fflush(stdout); // важно сбрасывать буфер
        sleep(1);
    }
    printf("\nDone!\n");
    return 0;
}