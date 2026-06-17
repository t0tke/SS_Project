#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <utility>

// ---------- per-object-file structures ----------

struct ObjSymbol {
    std::string name;
    std::string type;    // "SECTION", "LABEL", "CONST", "UND"
    uint32_t    value;
    std::string section; // section name, or "UND"/"ABS"
    bool        isGlobal;
};

struct ObjReloc {
    uint32_t    offset;
    std::string type;    // "ABS_32"
    std::string symbol;
    int32_t     addend;
};

struct ObjSection {
    std::string             name;
    std::vector<uint8_t>    data;
    std::vector<ObjReloc>   relocs;
};

struct ObjectFile {
    std::string                         filename;
    std::map<std::string, ObjSymbol>    symbols;
    std::vector<std::string>            sectionOrder;   // insertion order
    std::map<std::string, ObjSection>   sections;
};

// ---------- merged / linker structures ----------

struct SectionPiece {
    int      objIdx;
    uint32_t offsetInMerged;
    uint32_t size;
};

struct MergedSection {
    std::string               name;
    uint32_t                  totalSize;
    uint32_t                  baseAddr;
    std::vector<uint8_t>      data;
    std::vector<SectionPiece> pieces;
};

struct GlobalSym {
    uint32_t    finalAddr;
    int         objIdx;
    std::string section;
};

// ---------- Linker class ----------

class Linker {
public:
    int run(int argc, char* argv[]);

private:
    std::vector<ObjectFile>   objects_;
    std::vector<std::string>  inputFiles_;
    std::vector<std::string>  mergedOrder_;          // unique section names, first-appearance order

    std::map<std::string, MergedSection>  merged_;
    std::map<std::string, uint32_t>       placeMap_;  // -place overrides
    std::map<std::string, GlobalSym>      globalSyms_;

    // key = (objIdx, sectionName) → absolute base address of that piece
    std::map<std::pair<int,std::string>, uint32_t> sectionBases_;

    std::string outFile_;
    bool hexMode_;
    bool relocMode_;

    bool parseArgs   (int argc, char* argv[]);
    bool loadObject  (const std::string& filename);
    void mergeSections();
    bool placeSections();
    bool checkOverlaps();
    bool buildGlobalSymtab();
    bool applyRelocations();
    void writeHex();
    bool checkMultipleDefs();
    void writeRelocatable();
};
