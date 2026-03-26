#include <iostream>
int main() {
#if defined(__AVX512F__)
    std::cout << "AVX512" << std::endl;
#elif defined(__AVX2__)
    std::cout << "AVX2" << std::endl;
#elif defined(__AVX__)
    std::cout << "AVX" << std::endl;
#else
    std::cout << "noAVX" << std::endl;
#endif
    return 0;
}
