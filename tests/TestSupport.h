#pragma once
#include <cstdlib>
#include <iostream>
#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #expr << std::endl; \
    std::exit(1); \
} } while (0)
