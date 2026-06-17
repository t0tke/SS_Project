// linker.cpp – SS 2025/2026 – Linker
// Parses assembler .o output, merges sections, resolves symbols, applies relocations.

#include "linker.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <set>

// ======================================================================
//  Argument parsing
// ======================================================================

bool Linker::parseArgs(int argc, char* argv[]) {
    hexMode_   = false;
    relocMode_ = false;
    outFile_   = "a.out";

    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);

        if (arg == "-o" && i + 1 < argc) {
            outFile_ = argv[++i];
        }
        else if (arg == "-hex") {
            hexMode_ = true;
        }
        else if (arg == "-relocatable") {
            relocMode_ = true;
        }
        else if (arg.rfind("-place=", 0) == 0 || arg.rfind("--place=", 0) == 0) {
            size_t eq  = arg.find('=');
            std::string rest = arg.substr(eq + 1);
            size_t at = rest.find('@');
            if (at == std::string::npos) {
                std::cerr << "Error: neispravan -place format: " << arg << "\n";
                return false;
            }
            std::string secName = rest.substr(0, at);
            std::string addrStr = rest.substr(at + 1);
            uint32_t addr = (uint32_t)strtoul(addrStr.c_str(), nullptr, 0);
            placeMap_[secName] = addr;
        }
        else {
            inputFiles_.push_back(arg);
        }
    }

    if (!hexMode_ && !relocMode_) {
        std::cerr << "Error: mora se navesti -hex ili -relocatable\n";
        return false;
    }
    if (hexMode_ && relocMode_) {
        std::cerr << "Error: ne mogu obe opcije -hex i -relocatable istovremeno\n";
        return false;
    }
    if (inputFiles_.empty()) {
        std::cerr << "Error: nema ulaznih datoteka\n";
        return false;
    }
    return true;
}

// ======================================================================
//  .o file loader  (text ELF-like format)
// ======================================================================

bool Linker::loadObject(const std::string& filename) {
    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Error: ne mogu otvoriti '" << filename << "'\n";
        return false;
    }

    ObjectFile obj;
    obj.filename = filename;

    enum State { NONE, SYMTAB, SECDATA, RELA } state = NONE;
    std::string curSec;
    std::string line;
    std::map<std::string, uint32_t> expectedSizes;

    while (std::getline(f, line)) {
        // trim trailing whitespace / CR
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();

        if (line.empty()) continue;

        // ---- state transitions ----
        if (line == "#SYMTAB") { state = SYMTAB; continue; }

        if (line.substr(0, 8) == "#SECTION") {
            state = SECDATA;
            std::istringstream iss(line);
            std::string tag, name, sizeField;
            iss >> tag >> name >> sizeField;       // "#SECTION" "my_handler" "size=104"
            curSec = name;
            if (!obj.sections.count(curSec)) {
                ObjSection sec; sec.name = curSec;
                obj.sections[curSec] = sec;
                obj.sectionOrder.push_back(curSec);
            }
            // Parse expected size
            size_t eqPos = sizeField.find('=');
            if (eqPos != std::string::npos)
                expectedSizes[curSec] =
                    (uint32_t)strtoul(sizeField.substr(eqPos + 1).c_str(), nullptr, 10);
            continue;
        }

        if (line.substr(0, 5) == "#RELA") {
            state = RELA;
            std::istringstream iss(line);
            std::string tag;
            iss >> tag >> curSec;                   // "#RELA" "my_handler"
            continue;
        }

        // ---- skip header / separator lines ----
        if (line.find("Name") != std::string::npos &&
            line.find("Type") != std::string::npos &&
            line.find("Value") != std::string::npos)
            continue;

        if (line.find("Offset") != std::string::npos &&
            line.find("Type") != std::string::npos &&
            line.find("Symbol") != std::string::npos)
            continue;

        if (!line.empty() && line[0] == '-' && line.find_first_not_of('-') == std::string::npos)
            continue;

        // ---- parse depending on state ----
        switch (state) {

        case SYMTAB: {
            std::istringstream iss(line);
            std::string name, type, valStr, section, globalStr;
            if (!(iss >> name >> type >> valStr >> section >> globalStr)) break;

            ObjSymbol sym;
            sym.name     = name;
            sym.type     = type;
            sym.value    = (uint32_t)strtoul(valStr.c_str(), nullptr, 0);
            sym.section  = section;
            sym.isGlobal = (globalStr == "yes");
            obj.symbols[name] = sym;
            break;
        }

        case SECDATA: {
            size_t colon = line.find(':');
            if (colon == std::string::npos) break;
            std::istringstream iss(line.substr(colon + 1));
            std::string tok;
            while (iss >> tok)
                obj.sections[curSec].data.push_back(
                    (uint8_t)strtoul(tok.c_str(), nullptr, 16));
            break;
        }

        case RELA: {
            std::istringstream iss(line);
            std::string offStr, type, symbol;
            int32_t addend = 0;
            if (!(iss >> offStr >> type >> symbol >> addend)) break;

            ObjReloc rel;
            rel.offset = (uint32_t)strtoul(offStr.c_str(), nullptr, 0);
            rel.type   = type;
            rel.symbol = symbol;
            rel.addend = addend;
            obj.sections[curSec].relocs.push_back(rel);
            break;
        }

        default: break;
        }
    }

    // Verify section sizes match declared size=N
    for (auto& secKV : obj.sections) {
        auto it = expectedSizes.find(secKV.first);
        if (it != expectedSizes.end() &&
            it->second != (uint32_t)secKV.second.data.size()) {
            std::cerr << "Error: sekcija '" << secKV.first << "' u '"
                      << filename << "': deklarisano " << it->second
                      << " bajtova, procitano "
                      << secKV.second.data.size() << "\n";
            return false;
        }
    }

    objects_.push_back(std::move(obj));
    return true;
}

// ======================================================================
//  Merge same-named sections from all objects
// ======================================================================

void Linker::mergeSections() {
    // 1. Determine unique section names in first-appearance order
    std::set<std::string> seen;
    for (auto& obj : objects_) {
        for (auto& sn : obj.sectionOrder) {
            if (seen.insert(sn).second)
                mergedOrder_.push_back(sn);
        }
    }

    // 2. Build merged sections
    for (auto& sn : mergedOrder_) {
        MergedSection ms;
        ms.name      = sn;
        ms.totalSize = 0;
        ms.baseAddr  = 0;

        for (int i = 0; i < (int)objects_.size(); i++) {
            auto it = objects_[i].sections.find(sn);
            if (it == objects_[i].sections.end()) continue;

            SectionPiece piece;
            piece.objIdx         = i;
            piece.offsetInMerged = ms.totalSize;
            piece.size           = (uint32_t)it->second.data.size();
            ms.pieces.push_back(piece);

            ms.data.insert(ms.data.end(),
                           it->second.data.begin(),
                           it->second.data.end());
            ms.totalSize += piece.size;
        }

        merged_[sn] = std::move(ms);
    }
}

// ======================================================================
//  Place sections at final addresses
// ======================================================================

bool Linker::placeSections() {
    // 1.  Sections with explicit -place
    uint32_t highestEnd = 0;

    for (auto& kv : placeMap_) {
        auto it = merged_.find(kv.first);
        if (it == merged_.end()) {
            std::cerr << "Error: -place za nepostojecu sekciju '" << kv.first << "'\n"; 
            return false;
        }
        it->second.baseAddr = kv.second;
        uint32_t end = kv.second + it->second.totalSize;
        if (end < kv.second) {
            std::cerr << "Error: adresa sekcije '" << kv.first << "' prelazi 32-bitni adresni prostor\n";
            return false;
        }
        if (end > highestEnd) highestEnd = end;
    }

    // 2.  Remaining sections: placed contiguously after the highest end
    for (auto& sn : mergedOrder_) {
        if (placeMap_.count(sn)) continue;
        merged_[sn].baseAddr = highestEnd;
        uint32_t end = highestEnd + merged_[sn].totalSize;
        if (end < highestEnd) {
            std::cerr << "Error: adresa sekcije '" << sn << "' prelazi 32-bitni adresni prostor\n";
            return false;
        }
        highestEnd = end;
    }

    // 3.  Compute per-object section bases (absolute)
    for (auto& kv : merged_) {
        MergedSection& ms = kv.second;
        for (auto& piece : ms.pieces) {
            uint32_t base = ms.baseAddr + piece.offsetInMerged;
            sectionBases_[{piece.objIdx, ms.name}] = base;
        }
    }

    return true;
}

// ======================================================================
//  Check for section overlaps
// ======================================================================

bool Linker::checkOverlaps() {
    struct Range { uint32_t lo, hi; std::string name; };
    std::vector<Range> ranges;

    for (auto& kv : merged_) {
        if (kv.second.totalSize == 0) continue;
        ranges.push_back({kv.second.baseAddr,
                          kv.second.baseAddr + kv.second.totalSize,
                          kv.first});
    }

    for (size_t i = 0; i < ranges.size(); i++) {
        for (size_t j = i + 1; j < ranges.size(); j++) {
            if (ranges[i].lo < ranges[j].hi && ranges[j].lo < ranges[i].hi) {
                std::cerr << "Error: sekcije '" << ranges[i].name
                          << "' i '" << ranges[j].name << "' se preklapaju\n";
                return false;
            }
        }
    }
    return true;
}

// ======================================================================
//  Build global symbol table  (final addresses)
// ======================================================================

bool Linker::buildGlobalSymtab() {
    bool ok = true;

    // Pass 1 – collect all global definitions
    for (int i = 0; i < (int)objects_.size(); i++) {
        for (auto& kv : objects_[i].symbols) {
            const ObjSymbol& sym = kv.second;

            // Section symbols (.xxx) are local per object – skip
            if (!sym.name.empty() && sym.name[0] == '.' && sym.type == "SECTION")
                continue;

            // UND – will be checked later
            if (sym.type == "UND") continue;

            // Only globals go into the shared table
            if (!sym.isGlobal) continue;

            // Compute final address
            uint32_t finalAddr;
            if (sym.section == "ABS" && sym.type == "CONST") {
                finalAddr = sym.value;                       // absolute constant
            } else {
                auto baseIt = sectionBases_.find({i, sym.section});
                if (baseIt == sectionBases_.end()) {
                    std::cerr << "Error: simbol '" << sym.name
                              << "' referiše nepoznatu sekciju '" << sym.section << "'\n";
                    ok = false; continue;
                }
                finalAddr = baseIt->second + sym.value;
            }

            if (globalSyms_.count(sym.name)) {
                std::cerr << "Error: višestruka definicija simbola '"
                          << sym.name << "'\n";
                ok = false; continue;
            }

            GlobalSym gs;
            gs.finalAddr = finalAddr;
            gs.objIdx    = i;
            gs.section   = sym.section;
            globalSyms_[sym.name] = gs;
        }
    }
    if (!ok) return false;

    // Pass 2 – verify every relocation can be resolved
    for (int i = 0; i < (int)objects_.size(); i++) {
        for (auto& secKV : objects_[i].sections) {
            for (auto& rel : secKV.second.relocs) {
                if (!rel.symbol.empty() && rel.symbol[0] == '.')
                    continue;                                // section symbol → per-object
                if (!globalSyms_.count(rel.symbol)) {
                    std::cerr << "Error: nedefinisan simbol '"
                              << rel.symbol << "'\n";
                    ok = false;
                }
            }
        }
    }
    return ok;
}

// ======================================================================
//  Apply relocations  (ABS_32 – RELA style)
// ======================================================================

bool Linker::applyRelocations() {

    for (int i = 0; i < (int)objects_.size(); i++) {
        for (auto& secKV : objects_[i].sections) {
            const std::string& secName = secKV.first;
            const ObjSection&  objSec  = secKV.second;

            auto msIt = merged_.find(secName);
            if (msIt == merged_.end()) continue;
            MergedSection& ms = msIt->second;

            // Find this object's piece offset inside the merged section
            uint32_t pieceOff = 0;
            for (auto& p : ms.pieces) {
                if (p.objIdx == i) { pieceOff = p.offsetInMerged; break; }
            }

            for (auto& rel : objSec.relocs) {
                uint32_t patchPos = pieceOff + rel.offset;

                if (patchPos + 3 >= ms.data.size()) {
                    std::cerr << "Error: relokacija van granica sekcije '"
                              << secName << "' na offsetu "
                              << std::hex << rel.offset << std::dec << "\n";
                    return false;
                }

                // Resolve symbol value
                uint32_t symVal = 0;

                if (!rel.symbol.empty() && rel.symbol[0] == '.') {
                    // Section symbol – resolve from the same object
                    std::string refSec = rel.symbol.substr(1);
                    auto baseIt = sectionBases_.find({i, refSec});
                    if (baseIt == sectionBases_.end()) {
                        std::cerr << "Error: sekcija '" << refSec
                                  << "' ne postoji u objektu '"
                                  << objects_[i].filename << "'\n";
                        return false;
                    }
                    symVal = baseIt->second;
                } else {
                    // Global / extern symbol
                    auto gsIt = globalSyms_.find(rel.symbol);
                    if (gsIt == globalSyms_.end()) {
                        std::cerr << "Error: nedefinisan simbol '"
                                  << rel.symbol << "'\n";
                        return false;
                    }
                    symVal = gsIt->second.finalAddr;
                }

                uint32_t finalVal = symVal + (uint32_t)rel.addend;

                if (rel.type == "ABS_32") {
                    // little-endian 32-bit write
                    ms.data[patchPos + 0] = (uint8_t)( finalVal        & 0xFF);
                    ms.data[patchPos + 1] = (uint8_t)((finalVal >>  8) & 0xFF);
                    ms.data[patchPos + 2] = (uint8_t)((finalVal >> 16) & 0xFF);
                    ms.data[patchPos + 3] = (uint8_t)((finalVal >> 24) & 0xFF);
                } else {
                    std::cerr << "Error: nepoznat tip relokacije '"
                              << rel.type << "'\n";
                    return false;
                }
            }
        }
    }
    return true;
}

// ======================================================================
//  Write -hex output
// ======================================================================

void Linker::writeHex() {
    // Sort sections by base address
    std::vector<std::pair<uint32_t, const MergedSection*>> sorted;
    for (auto& kv : merged_) {
        if (kv.second.data.empty()) continue;
        sorted.push_back({kv.second.baseAddr, &kv.second});
    }
    std::sort(sorted.begin(), sorted.end());

    std::ofstream f(outFile_);
    if (!f.is_open()) {
        std::cerr << "Error: ne mogu kreirati izlaz '" << outFile_ << "'\n";
        return;
    }

    for (size_t si = 0; si < sorted.size(); si++) {
        uint32_t base = sorted[si].first;
        const MergedSection* sec = sorted[si].second;

        for (size_t i = 0; i < sec->data.size(); i += 8) {
            f << std::hex << std::uppercase
              << std::setw(8) << std::setfill('0') << (base + (uint32_t)i)
              << ":";
            for (size_t j = i; j < i + 8 && j < sec->data.size(); j++) {
                f << " " << std::hex << std::uppercase
                  << std::setw(2) << std::setfill('0')
                  << (unsigned)sec->data[j];
            }
            f << "\n";
        }
    }
}

// ======================================================================
//  -relocatable: check only for multiple definitions (UND is allowed)
// ======================================================================

bool Linker::checkMultipleDefs() {
    bool ok = true;
    std::set<std::string> seen;

    for (int i = 0; i < (int)objects_.size(); i++) {
        for (auto& kv : objects_[i].symbols) {
            const ObjSymbol& sym = kv.second;
            if (!sym.name.empty() && sym.name[0] == '.' && sym.type == "SECTION")
                continue;
            if (sym.type == "UND") continue;
            if (!sym.isGlobal) continue;

            if (!seen.insert(sym.name).second) {
                std::cerr << "Error: visestruka definicija simbola '"
                          << sym.name << "'\n";
                ok = false;
            }
        }
    }
    return ok;
}

// ======================================================================
//  -relocatable: write merged object in same text format as assembler
// ======================================================================

void Linker::writeRelocatable() {
    std::ofstream f(outFile_);
    if (!f.is_open()) {
        std::cerr << "Error: ne mogu kreirati izlaz '" << outFile_ << "'\n";
        return;
    }

    // ------ piece offsets (all sections from addr 0) ------
    // sectionBases_ not populated yet; compute piece offsets locally
    // key: (objIdx, secName) → offset within merged section
    std::map<std::pair<int,std::string>, uint32_t> pieceOff;
    for (auto& kv : merged_) {
        for (auto& p : kv.second.pieces)
            pieceOff[{p.objIdx, kv.first}] = p.offsetInMerged;
    }

    // ============ #SYMTAB ============
    f << "#SYMTAB\n"
      << std::left << std::setfill(' ')
      << std::setw(20) << "Name"
      << std::setw(10) << "Type"
      << std::setw(14) << "Value"
      << std::setw(16) << "Section"
      << "Global\n"
      << std::string(63, '-') << "\n";

    // a) Section symbols (one per merged section, value=0, local)
    for (auto& sn : mergedOrder_) {
        std::string key = "." + sn;
        f << std::left  << std::setfill(' ') << std::setw(20) << key
          << std::setw(10) << "SECTION"
          << "0x" << std::hex << std::internal
          << std::setw(8) << std::setfill('0') << 0
          << std::left  << std::setfill(' ')
          << "  " << std::setw(16) << sn << "no\n";
    }

    // b) Collect unique UND symbols
    std::set<std::string> emittedGlobal;
    std::set<std::string> definedGlobal;

    for (auto& obj : objects_)
        for (auto& kv : obj.symbols) {
            const ObjSymbol& sym = kv.second;
            if (!sym.name.empty() && sym.name[0] == '.') continue;
            if (sym.type == "UND" || !sym.isGlobal) continue;
            definedGlobal.insert(sym.name);
        }

    // c) Global defined symbols (adjusted offset)
    for (int i = 0; i < (int)objects_.size(); i++) {
        for (auto& kv : objects_[i].symbols) {
            const ObjSymbol& sym = kv.second;
            if (!sym.name.empty() && sym.name[0] == '.') continue;
            if (sym.type == "UND") continue;
            if (!sym.isGlobal) continue;
            if (emittedGlobal.count(sym.name)) continue;
            emittedGlobal.insert(sym.name);

            uint32_t val = sym.value;
            std::string sec = sym.section;
            std::string type = sym.type;

            if (sec != "ABS" && sec != "UND") {
                auto pit = pieceOff.find({i, sec});
                if (pit != pieceOff.end()) val += pit->second;
            }

            f << std::left  << std::setfill(' ') << std::setw(20) << sym.name
              << std::setw(10) << type
              << "0x" << std::hex << std::internal
              << std::setw(8) << std::setfill('0') << val
              << std::left  << std::setfill(' ')
              << "  " << std::setw(16) << sec
              << "yes\n";
        }
    }

    // d) UND symbols (only those not defined anywhere)
    std::set<std::string> emittedUnd;
    for (auto& obj : objects_) {
        for (auto& kv : obj.symbols) {
            const ObjSymbol& sym = kv.second;
            if (sym.type != "UND") continue;
            if (!sym.isGlobal) continue;
            if (definedGlobal.count(sym.name)) continue;
            if (emittedUnd.count(sym.name)) continue;
            emittedUnd.insert(sym.name);

            f << std::left  << std::setfill(' ') << std::setw(20) << sym.name
              << std::setw(10) << "UND"
              << "0x" << std::hex << std::internal
              << std::setw(8) << std::setfill('0') << 0
              << std::left  << std::setfill(' ')
              << "  " << std::setw(16) << "UND"
              << "yes\n";
        }
    }

    f << "\n";

    // ============ Sections + Relocs ============
    for (auto& sn : mergedOrder_) {
        MergedSection& ms = merged_[sn];

        // #SECTION
        f << "#SECTION " << sn << " size=" << std::dec << ms.data.size() << "\n";
        for (size_t i = 0; i < ms.data.size(); i++) {
            if (i % 16 == 0) {
                if (i) f << "\n";
                f << std::hex << std::right
                  << std::setw(4) << std::setfill('0') << i << ": ";
            }
            f << std::hex << std::setw(2) << std::setfill('0')
              << (unsigned)ms.data[i] << " ";
        }
        if (!ms.data.empty()) f << "\n";

        // #RELA — gather adjusted relocs from every piece
        std::vector<ObjReloc> mergedRelocs;

        for (auto& piece : ms.pieces) {
            int oi = piece.objIdx;
            auto secIt = objects_[oi].sections.find(sn);
            if (secIt == objects_[oi].sections.end()) continue;

            for (auto& rel : secIt->second.relocs) {
                ObjReloc mr;
                mr.offset = rel.offset + piece.offsetInMerged;
                mr.type   = rel.type;
                mr.symbol = rel.symbol;
                mr.addend = rel.addend;

                // If the symbol is a section symbol from the same object,
                // adjust addend by that section's piece offset
                if (!rel.symbol.empty() && rel.symbol[0] == '.') {
                    std::string refSec = rel.symbol.substr(1);
                    auto pit = pieceOff.find({oi, refSec});
                    if (pit != pieceOff.end())
                        mr.addend += (int32_t)pit->second;
                    // The merged section symbol covers the whole merged
                    // section, so the symbol name stays the same.
                }

                mergedRelocs.push_back(mr);
            }
        }

        if (!mergedRelocs.empty()) {
            f << "#RELA " << sn << "\n"
              << std::left << std::setfill(' ')
              << std::setw(12) << "Offset"
              << std::setw(10) << "Type"
              << std::setw(22) << "Symbol"
              << "Addend\n"
              << std::string(52, '-') << "\n";

            for (auto& r : mergedRelocs) {
                f << "0x" << std::hex << std::internal
                  << std::setw(8) << std::setfill('0') << r.offset
                  << std::left  << std::setfill(' ')
                  << "  " << std::setw(10) << r.type
                  << std::setw(22) << r.symbol
                  << std::dec << r.addend << "\n";
            }
        }
        f << "\n";
    }
}

// ======================================================================
//  Main entry point
// ======================================================================

int Linker::run(int argc, char* argv[]) {
    if (!parseArgs(argc, argv))   return 1;

    for (auto& file : inputFiles_)
        if (!loadObject(file))    return 1;

    mergeSections();

    if (hexMode_) {
        if (!placeSections())         return 1;
        if (!checkOverlaps())         return 1;
        if (!buildGlobalSymtab())     return 1;
        if (!applyRelocations())      return 1;
        writeHex();
    } else if (relocMode_) {
        if (!checkMultipleDefs())     return 1;
        writeRelocatable();
    }

    return 0;
}

// ======================================================================

int main(int argc, char* argv[]) {
    Linker linker;
    return linker.run(argc, argv);
}