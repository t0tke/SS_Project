#pragma once

#include "assembler.hpp"   // SymbolType, symTypeName, Symbol
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <ostream>
#include <istream>

struct ObjReloc {
    uint32_t    offset;
    std::string symbol;
    int32_t     addend;
};

struct ObjSectionData {
    std::vector<uint8_t>   data;
    std::vector<ObjReloc>  relocs;
};

// Neutralan model predmetnog fajla – isti oblik puni asembler (pri pisanju) i reader (pri čitanju)
struct ObjectModel {
    std::vector<std::string>              sectionOrder; 
    std::map<std::string, Symbol>         symtab;  // sad imamo nazive .section za lokalne i naziv simbola za globalne
    std::map<std::string, ObjSectionData> sections;    
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
    static const uint8_t  T_UND        = 0;
    static const uint8_t  T_LABEL      = 1;
    static const uint8_t  T_SECTION    = 2;
}


void objWriteText(std::ostream& os, const ObjectModel& m);

void objWriteBinary(std::ostream& os, const ObjectModel& m);
bool objReadBinary(std::istream& is, ObjectModel& m);
