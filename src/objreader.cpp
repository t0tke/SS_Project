// objreader.cpp – SS 2025/2026
// Pomoćni alat: učita sopstveni binarni predmetni fajl ("SSOB") i ispiše
// njegov tekstualni prikaz na stdout. Služi za round-trip proveru
// (objreader x.o  treba da bude identičan fajlu  x.o.txt).
#include "objfile.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Upotreba: objreader ulaz.o\n");
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Error: ne mogu da otvorim '%s'\n", argv[1]);
        return 1;
    }
    ObjectModel m;
    if (!objReadBinary(in, m)) {
        std::fprintf(stderr, "Error: '%s' nije validan SSOB predmetni fajl\n", argv[1]);
        return 1;
    }
    objWriteText(std::cout, m);
    return 0;
}
