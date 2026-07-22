#pragma once
// linker.hpp – SS 2025/2026
// Linker nivoa B za sopstveni binarni SSOB predmetni format.
// Radi nad zajedničkim objektnim modelom (ObjectModel) iz objfile.hpp:
// isti model puni asembler pri pisanju i linker pri čitanju.

#include "objfile.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <utility>

// ---------- učitani ulazni predmetni program ----------
struct LoadedObject {
    std::string filename;
    ObjectModel model;
};

// ---------- agregacija istoimenih sekcija ----------
// Parče = doprinos jednog objekta jednoj agregiranoj sekciji.
struct SectionPiece {
    int      objIdx;          // indeks u objects_
    uint32_t offsetInMerged;  // ofset ovog parčeta u agregiranoj sekciji
};

struct MergedSection {
    uint32_t                  totalSize = 0;
    uint32_t                  baseAddr  = 0;   // konačna adresa (posle placeSections)
    std::vector<uint8_t>      data;            // spojeni bajtovi svih parčića
    std::vector<SectionPiece> pieces;
};

// ---------- globalna (izvezena) definicija simbola ----------
struct GlobalDef {
    int         objIdx;    // objekat u kome je simbol definisan
    std::string section;   // sekcija u kojoj je definisan
    uint32_t    value;     // ofset unutar te sekcije
};

// ---------- Linker ----------
class Linker {
public:
    int run(int argc, char* argv[]);

private:
    // ulaz / opcije
    std::vector<std::string>  inputFiles_;
    std::string               outFile_;
    bool                      haveOut_   = false;
    bool                      hexMode_   = false;
    bool                      relocMode_ = false;
    std::map<std::string, uint32_t> placeMap_;   // ime sekcije -> zadata adresa

    // učitani objekti i izvedene strukture
    std::vector<LoadedObject>            objects_;
    std::vector<std::string>             mergedOrder_;   // sekcije, redosled prvog pojavljivanja
    std::map<std::string, MergedSection> merged_;
    std::map<std::string, GlobalDef>     globalDefs_;    // izvezeni definisani simboli
    // (objIdx, ime sekcije) -> apsolutna bazna adresa tog parčeta
    std::map<std::pair<int, std::string>, uint32_t> sectionBases_;

    // faze rada
    bool parseArgs(int argc, char* argv[]);
    bool loadObject(const std::string& filename);
    bool mergeSections();
    bool collectGlobalDefs();                        // + detekcija višestrukih definicija
   
    //hex
    bool placeSections();                            // -place + podrazumevano
    bool checkOverlaps();
    bool checkUnresolved();                          // samo -hex
    bool applyRelocations();                         // samo -hex
    bool writeHex();
    
    //relocatable
    bool writeRelocatable();

    // pomoćno
    uint32_t sectionBase(int objIdx, const std::string& sec, bool& ok) const;
};
