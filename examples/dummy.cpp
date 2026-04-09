#include <iostream>
#include <unistd.h>

int main() {
    std::cout << "Dummy program starting...\n";
    for(int i = 0; i < 50; ++i) {
        std::cout << "Tick " << i << "\n";
        sleep(1);
    }
    std::cout << "Dummy program exiting.\n";
    return 0;
}