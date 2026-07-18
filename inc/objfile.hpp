#pragma once
// objfile.hpp – SS 2025/2026
// Zajednički model predmetnog fajla + I/O za sopstveni binarni format "SSOB".
//
// Binarni format je namerno mali (inspirisan ELF-om, ali NIJE pravi ELF):
// čuva tačno ono što je potrebno linkeru nivoa B — redosled sekcija, sirove
// bajtove sekcija, tabelu simbola i relokacije (bez tipa, sve su ABS_32).
// Detaljna specifikacija po redosledu bajtova nalazi se u objfile.cpp.

#include "assembler.hpp"   // SymbolType, symTypeName, Symbol
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <ostream>
#include <istream>

// Jedan relokacioni zapis unutar sekcije (tip se ne čuva – uvek ABS_32).
struct ObjReloc {
    uint32_t    offset;
    std::string symbol;
    int32_t     addend;
};

// Sirovi sadržaj jedne sekcije + njene relokacije.
struct ObjSectionData {
    std::vector<uint8_t>   data;
    std::vector<ObjReloc>  relocs;
};

// Neutralan model predmetnog fajla – isti oblik puni asembler (pri pisanju)
// i reader (pri čitanju), pa je round-trip provera tekstualni diff.
struct ObjectModel {
    std::vector<std::string>              sectionOrder; // redosled definisanja sekcija
    std::map<std::string, Symbol>         symtab;       // ime -> simbol (".sekcija" za SECTION)
    std::map<std::string, ObjSectionData> sections;     // ime sekcije -> sadržaj
};

// ---- Format konstante ----
namespace objfmt {
    static const uint8_t  MAGIC[4]     = {'S','S','O','B'};
    static const uint16_t VERSION      = 1;
    static const uint16_t ENDIAN_LE    = 1;
    static const uint32_t HEADER_SIZE  = 32;
    static const uint32_t SEC_ENT_SIZE = 12;
    static const uint32_t SYM_ENT_SIZE = 16;
    static const uint32_t REL_ENT_SIZE = 16;
    // Kod tipa simbola u binarnom fajlu (poklapa se sa enum class SymbolType).
    static const uint8_t  T_UND        = 0;
    static const uint8_t  T_LABEL      = 1;
    static const uint8_t  T_SECTION    = 2;
}

// Tekstualni prikaz (isti izgled koji je asembler oduvek generisao).
void objWriteText(std::ostream& os, const ObjectModel& m);

// Binarni zapis / čitanje sopstvenog formata "SSOB".
void objWriteBinary(std::ostream& os, const ObjectModel& m);
bool objReadBinary(std::istream& is, ObjectModel& m);
