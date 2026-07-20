// linker.cpp – SS 2025/2026 – Linker (nivo B)
//
// Učitava sopstveni binarni SSOB predmetni format preko zajedničkog
// objektnog modela (objfile.hpp), agregira istoimene sekcije, razrešava
// simbole i relokacije i generiše izlaz u -hex ili -relocatable režimu.
//
// Tok rada (run): najpre se UČITAJU I VALIDIRAJU svi ulazni fajlovi; tek
// potom se formiraju agregirane sekcije, tabela simbola i relokacije, pa
// se izvršava povezivanje.

#include "linker.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <set>

// ======================================================================
//  Argumenti komandne linije
// ======================================================================
bool Linker::parseArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);

        if (arg == "-o") {
            if (i + 1 >= argc) { std::cerr << "Error: -o zahteva naziv izlazne datoteke\n"; return false; }
            outFile_ = argv[++i]; haveOut_ = true;
        }
        else if (arg == "-hex")        { hexMode_ = true; }
        else if (arg == "-relocatable"){ relocMode_ = true; }
        else if (arg.rfind("-place=", 0) == 0) {
            std::string rest = arg.substr(std::string("-place=").size());
            size_t at = rest.find('@');
            if (at == std::string::npos) {
                std::cerr << "Error: neispravan -place format (očekivano -place=sekcija@adresa): " << arg << "\n";
                return false;
            }
            std::string secName = rest.substr(0, at);
            std::string addrStr = rest.substr(at + 1);
            if (secName.empty() || addrStr.empty()) {
                std::cerr << "Error: neispravan -place format: " << arg << "\n"; return false;
            }
            // Stroga validacija adrese: mora biti ceo broj, bez viška znakova,
            // bez negativnog predznaka i mora da stane u 32 bita.
            errno = 0;
            char* endp = nullptr;
            unsigned long long v = strtoull(addrStr.c_str(), &endp, 0);
            if (endp == addrStr.c_str() || *endp != '\0' ||
                addrStr[0] == '-' || addrStr[0] == '+') {
                std::cerr << "Error: neispravna -place adresa '" << addrStr << "' za sekciju '" << secName << "'\n";
                return false;
            }
            if (errno == ERANGE || v > 0xFFFFFFFFull) {
                std::cerr << "Error: -place adresa '" << addrStr << "' za sekciju '" << secName
                          << "' prelazi 32-bitni adresni prostor\n";
                return false;
            }
            if (placeMap_.count(secName)) {
                std::cerr << "Error: duplirani -place za sekciju '" << secName << "'\n";
                return false;
            }
            placeMap_[secName] = (uint32_t)v;
        }
        else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: nepoznata opcija '" << arg << "'\n"; return false;
        }
        else {
            inputFiles_.push_back(arg);
        }
    }

    // Tačno jedna od -hex / -relocatable je obavezna.
    if (hexMode_ && relocMode_) {
        std::cerr << "Error: navesti tačno jednu od opcija -hex i -relocatable (navedene obe)\n"; return false;
    }
    if (!hexMode_ && !relocMode_) {
        std::cerr << "Error: navesti tačno jednu od opcija -hex i -relocatable (nijedna nije navedena)\n"; return false;
    }
    if (inputFiles_.empty()) {
        std::cerr << "Error: nema ulaznih predmetnih datoteka\n"; return false;
    }
    if (!haveOut_) outFile_ = hexMode_ ? "out.hex" : "out.o";
    return true;
}

// ======================================================================
//  Učitavanje jednog SSOB predmetnog fajla + validacija
// ======================================================================
bool Linker::loadObject(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) { std::cerr << "Error: ne mogu da otvorim '" << filename << "'\n"; return false; }

    LoadedObject obj;
    obj.filename = filename;
    if (!objReadBinary(f, obj.model)) {
        std::cerr << "Error: '" << filename << "' nije validan SSOB predmetni fajl\n";
        return false;
    }
    if (!validateObject(obj)) return false;     // poruka je već ispisana
    objects_.push_back(std::move(obj));
    return true;
}

// Strukturna provera učitanog modela (pre bilo kakvog povezivanja).
bool Linker::validateObject(const LoadedObject& obj) {
    const ObjectModel& m = obj.model;

    // Svaka sekcija iz redosleda mora postojati.
    for (auto& sn : m.sectionOrder) {
        if (!m.sections.count(sn)) {
            std::cerr << "Error: '" << obj.filename << "': sekcija '" << sn
                      << "' navedena u redosledu ne postoji\n";
            return false;
        }
    }
    // Relokacije: postojanje ciljnog simbola/sekcije.
    // (Opseg r.offset+4 <= data.size() je već garantovao objReadBinary pri učitavanju.)
    for (auto& scKV : m.sections) {
        const ObjSectionData& sec = scKV.second;
        for (auto& r : sec.relocs) {
            if (!r.symbol.empty() && r.symbol[0] == '.') {
                std::string refSec = r.symbol.substr(1);
                if (!m.sections.count(refSec)) {
                    std::cerr << "Error: '" << obj.filename << "': relokacija referiše nepostojeću sekciju '"
                              << refSec << "'\n";
                    return false;
                }
            } else {
                auto sit = m.symtab.find(r.symbol);
                if (sit == m.symtab.end()) {
                    std::cerr << "Error: '" << obj.filename << "': relokacija referiše nepoznat simbol '"
                              << r.symbol << "'\n";
                    return false;
                }
                // Relokacija po imenu na LOKALAN simbol znači referencu na nedefinisan
                // lokalan simbol (definisan lokalan bi bio pretvoren u sekcijsku
                // relokaciju u asembleru). Takav simbol NIKAD ne sme da bude razrešen
                // globalnim simbolom istog imena iz drugog fajla.
                if (!sit->second.isGlobal) {
                    std::cerr << "Error: '" << obj.filename << "': referenca na nedefinisan lokalan simbol '"
                              << r.symbol << "' (nedostaje definicija ili .extern)\n";
                    return false;
                }
            }
        }
    }
    return true;
}

// ======================================================================
//  Agregacija istoimenih sekcija (redosled prvog pojavljivanja)
// ======================================================================
bool Linker::mergeSections() {
    std::set<std::string> seen;
    for (auto& obj : objects_)
        for (auto& sn : obj.model.sectionOrder)
            if (seen.insert(sn).second) mergedOrder_.push_back(sn);

    for (auto& sn : mergedOrder_) {
        MergedSection ms;
        for (int i = 0; i < (int)objects_.size(); i++) {
            auto it = objects_[i].model.sections.find(sn);
            if (it == objects_[i].model.sections.end()) continue;
            uint32_t pieceSize = (uint32_t)it->second.data.size();
            // Agregirana veličina/ofset moraju da stanu u 32-bitni adresni prostor;
            // preko toga bi se ofset "obmotao" i pokvario smeštanje i relokacije.
            if ((uint64_t)ms.totalSize + pieceSize > 0xFFFFFFFFull) {
                std::cerr << "Error: agregirana sekcija '" << sn
                          << "' prelazi 32-bitni adresni prostor\n";
                return false;
            }
            ms.pieces.push_back({ i, ms.totalSize });
            ms.data.insert(ms.data.end(), it->second.data.begin(), it->second.data.end());
            ms.totalSize += pieceSize;
        }
        merged_[sn] = std::move(ms);
    }
    return true;
}

// ======================================================================
//  Globalne (izvezene) definicije + detekcija višestrukih definicija
// ======================================================================
bool Linker::collectGlobalDefs() {
    bool ok = true;
    for (int i = 0; i < (int)objects_.size(); i++) {
        for (auto& kv : objects_[i].model.symtab) {
            const std::string& name = kv.first;
            const Symbol&      sym  = kv.second;
            if (sym.type == SymbolType::SECTION) continue;      // sekcijski simbol – lokalno
            if (!sym.isGlobal || !sym.isDefined) continue;      // samo izvezene definicije

            if (globalDefs_.count(name)) {
                std::cerr << "Error: višestruka definicija simbola '" << name << "'\n";
                ok = false; continue;
            }
            globalDefs_[name] = { i, sym.section, sym.value };
        }
    }
    return ok;
}

// ======================================================================
//  Smeštanje sekcija (-place + podrazumevano)
// ======================================================================
bool Linker::placeSections() {
    // Kraj (ekskluzivno) računamo u 64 bita: sekcija koja se završava TAČNO na
    // 2^32 je validna (zauzima do 0xFFFFFFFF), a preko toga je greška.
    const uint64_t LIMIT = 0x100000000ull;
    uint64_t highestEnd = 0;

    // 1) Sekcije zadate preko -place.
    for (auto& kv : placeMap_) {
        auto it = merged_.find(kv.first);
        if (it == merged_.end()) {
            std::cerr << "Error: -place za nepostojeću sekciju '" << kv.first << "'\n";
            return false;
        }
        it->second.baseAddr = kv.second;
        uint64_t end = (uint64_t)kv.second + it->second.totalSize;
        if (end > LIMIT) {
            std::cerr << "Error: sekcija '" << kv.first << "' prelazi 32-bitni adresni prostor\n";
            return false;
        }
        if (end > highestEnd) highestEnd = end;
    }

    // 2) Ostale sekcije: nadovezuju se odmah iza najviše zauzete adrese,
    //    u redosledu prvog pojavljivanja.
    for (auto& sn : mergedOrder_) {
        if (placeMap_.count(sn)) continue;
        // Baza mora biti VALIDNA 32-bitna adresa. Ako se prethodna sekcija
        // završila tačno na 0x100000000, ova (pa i prazna) bi počela van prostora;
        // provera je PRE kastovanja u uint32_t da se highestEnd ne bi obmotao na 0.
        if (highestEnd > 0xFFFFFFFFull) {
            std::cerr << "Error: sekcija '" << sn << "' počinje na adresi van 32-bitnog prostora\n";
            return false;
        }
        uint64_t end = highestEnd + merged_[sn].totalSize;
        if (end > LIMIT) {
            std::cerr << "Error: sekcija '" << sn << "' prelazi 32-bitni adresni prostor\n";
            return false;
        }
        merged_[sn].baseAddr = (uint32_t)highestEnd;
        highestEnd = end;
    }

    // 3) Apsolutne bazne adrese svakog parčeta. Račun je u 64 bita i proverava se
    //    PRE kastovanja: prazno parče na kraju sekcije koja se završava na
    //    0x100000000 imalo bi bazu 0x100000000 koja bi se obmotala na 0.
    for (auto& kv : merged_)
        for (auto& p : kv.second.pieces) {
            uint64_t base = (uint64_t)kv.second.baseAddr + p.offsetInMerged;
            if (base > 0xFFFFFFFFull) {
                std::cerr << "Error: parče sekcije '" << kv.first
                          << "' počinje na adresi van 32-bitnog prostora\n";
                return false;
            }
            sectionBases_[{p.objIdx, kv.first}] = (uint32_t)base;
        }

    return true;
}

// ======================================================================
//  Provera preklapanja sekcija
// ======================================================================
bool Linker::checkOverlaps() {
    struct Range { uint64_t lo, hi; std::string name; };   // 64-bit: hi sme biti 2^32
    std::vector<Range> rs;
    for (auto& kv : merged_) {
        if (kv.second.totalSize == 0) continue;
        rs.push_back({ kv.second.baseAddr, (uint64_t)kv.second.baseAddr + kv.second.totalSize, kv.first });
    }
    std::sort(rs.begin(), rs.end(), [](const Range& a, const Range& b){ return a.lo < b.lo; });
    for (size_t i = 0; i + 1 < rs.size(); i++) {
        if (rs[i].hi > rs[i + 1].lo) {
            std::cerr << "Error: sekcije '" << rs[i].name << "' i '" << rs[i + 1].name
                      << "' se preklapaju\n";
            return false;
        }
    }
    return true;
}

// ======================================================================
//  Provera nerazrešenih simbola (samo -hex)
// ======================================================================
bool Linker::checkUnresolved() {
    bool ok = true;
    std::set<std::string> reported;
    auto report = [&](const std::string& name) {
        if (reported.insert(name).second) {
            std::cerr << "Error: nerazrešen simbol '" << name << "'\n";
            ok = false;
        }
    };

    // (1) Svaki nedefinisan GLOBAL simbol (uključujući nekorišćene .extern) koji
    //     nije definisan ni u jednom ulazu je nerazrešen.
    for (auto& obj : objects_)
        for (auto& kv : obj.model.symtab) {
            const Symbol& s = kv.second;
            if (s.type == SymbolType::SECTION) continue;
            if (s.isGlobal && !s.isDefined && !globalDefs_.count(kv.first))
                report(kv.first);
        }

    // (2) Sigurnosna provera: svaka relokacija po imenu mora imati definiciju.
    //     (Lokalne nedefinisane reference su već odbijene u validateObject.)
    for (auto& obj : objects_)
        for (auto& scKV : obj.model.sections)
            for (auto& r : scKV.second.relocs) {
                if (!r.symbol.empty() && r.symbol[0] == '.') continue;
                if (!globalDefs_.count(r.symbol)) report(r.symbol);
            }

    return ok;
}

uint32_t Linker::sectionBase(int objIdx, const std::string& sec, bool& ok) const {
    auto it = sectionBases_.find({ objIdx, sec });
    if (it == sectionBases_.end()) { ok = false; return 0; }
    ok = true; return it->second;
}

// ======================================================================
//  Primena relokacija (ABS_32) – samo -hex
// ======================================================================
bool Linker::applyRelocations() {
    for (int i = 0; i < (int)objects_.size(); i++) {
        for (auto& scKV : objects_[i].model.sections) {
            const std::string&    sn  = scKV.first;
            const ObjSectionData& sec = scKV.second;

            MergedSection& ms = merged_[sn];
            uint32_t pieceOff = 0;
            for (auto& p : ms.pieces) if (p.objIdx == i) { pieceOff = p.offsetInMerged; break; }

            for (auto& r : sec.relocs) {
                uint32_t patchPos = pieceOff + r.offset;   // opseg već validiran pri učitavanju

                // Adresu računamo u 64 bita da bismo otkrili "obmotavanje":
                // simbol na kraju sekcije koja se završava tačno na 2^32 imao bi
                // adresu 0x100000000 koja se u 32 bita svodi na 0.
                bool ok = true;
                int64_t addr;
                if (!r.symbol.empty() && r.symbol[0] == '.') {
                    addr = (int64_t)sectionBase(i, r.symbol.substr(1), ok) + (int64_t)r.addend;
                } else {
                    // Simbol je garantovano razrešen: checkUnresolved() se izvršava pre
                    // applyRelocations() i odbija svaku relokaciju bez definicije.
                    const GlobalDef& gd = globalDefs_.at(r.symbol);
                    addr = (int64_t)sectionBase(gd.objIdx, gd.section, ok)
                         + (int64_t)gd.value + (int64_t)r.addend;
                }
                if (!ok) {
                    std::cerr << "Error: ne mogu da razrešim relokaciju u sekciji '" << sn << "'\n";
                    return false;
                }
                if (addr < 0 || addr > 0xFFFFFFFFll) {
                    std::cerr << "Error: relokacija u sekciji '" << sn << "' (simbol '"
                              << r.symbol << "') daje adresu van 32-bitnog prostora\n";
                    return false;
                }

                uint32_t v = (uint32_t)addr;
                ms.data[patchPos + 0] = (uint8_t)( v        & 0xFF);
                ms.data[patchPos + 1] = (uint8_t)((v >>  8) & 0xFF);
                ms.data[patchPos + 2] = (uint8_t)((v >> 16) & 0xFF);
                ms.data[patchPos + 3] = (uint8_t)((v >> 24) & 0xFF);
            }
        }
    }
    return true;
}

// ======================================================================
//  Izlaz -hex : parovi (adresa, sadržaj), 8 bajtova po redu
// ======================================================================
bool Linker::writeHex() {
    std::vector<const MergedSection*> secs;
    for (auto& kv : merged_) if (!kv.second.data.empty()) secs.push_back(&kv.second);
    std::sort(secs.begin(), secs.end(),
              [](const MergedSection* a, const MergedSection* b){ return a->baseAddr < b->baseAddr; });

    std::ofstream f(outFile_);
    if (!f) { std::cerr << "Error: ne mogu da kreiram izlaz '" << outFile_ << "'\n"; return false; }

    for (auto* s : secs) {
        for (size_t i = 0; i < s->data.size(); i += 8) {
            f << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
              << (uint32_t)(s->baseAddr + i) << ":";
            for (size_t j = i; j < i + 8 && j < s->data.size(); j++)
                f << " " << std::setw(2) << std::setfill('0') << (unsigned)s->data[j];
            f << "\n";
        }
    }
    f.flush();
    if (!f.good()) { std::cerr << "Error: greška pri pisanju izlaza '" << outFile_ << "'\n"; return false; }
    return true;
}

// ======================================================================
//  Izlaz -relocatable : spojeni predmetni program u SSOB formatu
//  (sve sekcije od nulte adrese; -place se ignoriše)
// ======================================================================
bool Linker::writeRelocatable() {
    // Ofset parčeta (objIdx, sekcija) unutar agregirane sekcije.
    std::map<std::pair<int, std::string>, uint32_t> pieceOff;
    for (auto& kv : merged_)
        for (auto& p : kv.second.pieces)
            pieceOff[{ p.objIdx, kv.first }] = p.offsetInMerged;

    ObjectModel out;
    out.sectionOrder = mergedOrder_;

    // --- Sekcije: podaci + prilagođene relokacije ---
    for (auto& sn : mergedOrder_) {
        MergedSection& ms = merged_[sn];
        ObjSectionData sd;
        sd.data = ms.data;                       // spojeni bajtovi (od adrese 0)

        for (auto& p : ms.pieces) {
            int oi = p.objIdx;
            auto secIt = objects_[oi].model.sections.find(sn);
            if (secIt == objects_[oi].model.sections.end()) continue;

            for (auto& r : secIt->second.relocs) {
                ObjReloc nr;
                nr.offset = r.offset + p.offsetInMerged;
                nr.symbol = r.symbol;
                nr.addend = r.addend;
                // Sekcijski simbol pokriva celu agregiranu sekciju -> addend
                // se pomera za ofset referisane sekcije u tom objektu. Račun je
                // u uint32 (modularno, dobro definisano): za ofset > 0x7FFFFFFF
                // bi `(int32_t)pieceOffset` bio negativan i signed-overflow (UB);
                // ovako je 32-bitni obrazac bita uvek ispravan.
                if (!r.symbol.empty() && r.symbol[0] == '.') {
                    auto pit = pieceOff.find({ oi, r.symbol.substr(1) });
                    if (pit != pieceOff.end())
                        nr.addend = (int32_t)((uint32_t)nr.addend + pit->second);
                }
                // Održavamo invarijantu: data[slot] == addend (kao kod asemblera).
                uint32_t a = (uint32_t)nr.addend;
                sd.data[nr.offset + 0] = (uint8_t)( a        & 0xFF);
                sd.data[nr.offset + 1] = (uint8_t)((a >>  8) & 0xFF);
                sd.data[nr.offset + 2] = (uint8_t)((a >> 16) & 0xFF);
                sd.data[nr.offset + 3] = (uint8_t)((a >> 24) & 0xFF);
                sd.relocs.push_back(nr);
            }
        }
        out.sections[sn] = std::move(sd);
    }

    // --- Simboli ---
    // a) sekcijski simbol za svaku agregiranu sekciju
    for (auto& sn : mergedOrder_) {
        Symbol s;
        s.type = SymbolType::SECTION; s.value = 0; s.section = sn;
        s.isGlobal = false; s.isDefined = true;
        out.symtab["." + sn] = s;
    }
    // b) globalne definisane simbole (vrednost pomerena za ofset parčeta)
    for (auto& kv : globalDefs_) {
        const std::string& name = kv.first;
        const GlobalDef&   gd   = kv.second;
        uint32_t off = 0;
        auto pit = pieceOff.find({ gd.objIdx, gd.section });
        if (pit != pieceOff.end()) off = pit->second;
        Symbol s;
        s.type = SymbolType::LABEL; s.value = gd.value + off; s.section = gd.section;
        s.isGlobal = true; s.isDefined = true;
        out.symtab[name] = s;
    }
    // c) preostale nerazrešene (UND) globalne simbole
    std::set<std::string> undEmitted;
    for (auto& obj : objects_) {
        for (auto& kv : obj.model.symtab) {
            const Symbol& sym = kv.second;
            if (sym.isDefined || sym.type == SymbolType::SECTION) continue;
            if (globalDefs_.count(kv.first)) continue;          // ipak definisan negde
            if (!undEmitted.insert(kv.first).second) continue;
            Symbol s; s.type = SymbolType::UND; s.value = 0; s.section = "UND";
            s.isGlobal = true; s.isDefined = false;
            out.symtab[kv.first] = s;
        }
    }

    std::ofstream fb(outFile_, std::ios::binary);
    if (!fb) { std::cerr << "Error: ne mogu da kreiram izlaz '" << outFile_ << "'\n"; return false; }
    objWriteBinary(fb, out);
    fb.flush();
    if (!fb.good()) { std::cerr << "Error: greška pri pisanju izlaza '" << outFile_ << "'\n"; return false; }

    std::ofstream ft(outFile_ + ".txt");            // čitljiv prikaz (pomoćni)
    if (ft) objWriteText(ft, out);
    else std::cerr << "Upozorenje: ne mogu da kreiram '" << outFile_ << ".txt'\n";
    return true;
}

// ======================================================================
//  Glavni tok
// ======================================================================
int Linker::run(int argc, char* argv[]) {
    if (!parseArgs(argc, argv)) return 1;

    // 1) Najpre učitaj i validiraj SVE ulazne fajlove.
    for (auto& file : inputFiles_)
        if (!loadObject(file)) return 1;

    // 2) Tek onda agregacija, tabela simbola i povezivanje.
    if (!mergeSections())     return 1;
    if (!collectGlobalDefs()) return 1;   // višestruke definicije (oba režima)

    if (hexMode_) {
        if (!placeSections())    return 1;
        if (!checkOverlaps())    return 1;
        if (!checkUnresolved())  return 1;
        if (!applyRelocations()) return 1;
        if (!writeHex())         return 1;
    } else { // relocMode_
        if (!writeRelocatable()) return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    Linker linker;
    return linker.run(argc, argv);
}
