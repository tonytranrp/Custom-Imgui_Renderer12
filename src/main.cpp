#include "MainRendering.hpp"
#include <Windows.h>

int main(int, char**) {
    return MainRendering::Run(GetModuleHandle(nullptr));
}
