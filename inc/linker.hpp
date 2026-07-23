#pragma once

#include "objfile.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <utility>

struct LoadedObject {
    ObjectModel model;
};

struct SectionPiece {
    int      objIdx;          // indeks u objects_
    uint32_t offsetInMerged;  
};

struct MergedSection {
    uint32_t                  totalSize = 0;
    uint32_t                  baseAddr  = 0;   // konačna adresa (posle placeSections)
    std::vector<uint8_t>      data;   
    std::vector<SectionPiece> pieces;
};

struct GlobalDef {
    int         objIdx;    // objekat u kome je simbol definisan
    std::string section;   
    uint32_t    value;     // ofset unutar te sekcije
};

class Linker {
public:
    int run(int argc, char* argv[]);

private:
    std::vector<std::string>  inputFiles_;
    std::string               outFile_;
    bool                      haveOut_   = false;
    bool                      hexMode_   = false;
    bool                      relocMode_ = false;
    std::map<std::string, uint32_t> placeMap_; 

    // učitani objekti i izvedene strukture
    std::vector<LoadedObject>            objects_;
    std::vector<std::string>             mergedOrder_;
    std::map<std::string, MergedSection> merged_;
    std::map<std::string, GlobalDef>     globalDefs_; 
    std::map<std::pair<int, std::string>, uint32_t> sectionBases_; // (objIdx, ime sekcije) -> apsolutna bazna adresa tog parčeta

    uint32_t sectionBase(int objIdx, const std::string& sec, bool& ok) const;

    //redosled metoda
    bool parseArgs(int argc, char* argv[]);
    bool loadObject(const std::string& filename);
    bool mergeSections();
    bool collectGlobalDefs();// + detekcija višestrukih definicija
   
    //hex
    bool placeSections();                            
    bool checkOverlaps();
    bool checkUnresolved();                     
    bool applyRelocations();                         
    bool writeHex();
    
    //relocatable
    bool writeRelocatable();

};
