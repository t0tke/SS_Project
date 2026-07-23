#include "objfile.hpp"
#include <iomanip>
#include <cstring>

//tekstualni prikaz
void objWriteText(std::ostream& f, const ObjectModel& m) {
    f << "#SYMTAB\n"
      << std::left << std::setfill(' ')
      << std::setw(20) << "Name"
      << std::setw(9)  << "Type"
      << std::setw(8)  << "Bind"
      << std::setw(5)  << "Def"
      << std::setw(18) << "Section"
      << "Value\n"
      << std::string(68, '-') << "\n";

    auto emitSym = [&](const std::string& name, const Symbol& sym) {
        f << std::left << std::setfill(' ')
          << std::setw(20) << name
          << std::setw(9)  << symTypeName(sym.type)
          << std::setw(8)  << (sym.isGlobal  ? "GLOBAL" : "LOCAL")
          << std::setw(5)  << (sym.isDefined ? "yes"    : "no")
          << std::setw(18) << sym.section
          << "0x" << std::hex << std::right << std::setw(8) << std::setfill('0') << sym.value
          << std::dec << std::setfill(' ') << "\n";
    };

    for (auto& sectionName : m.sectionOrder) {
        std::string key = "." + sectionName;
        auto it = m.symtab.find(key);
        if (it != m.symtab.end()) emitSym(key, it->second);
    }

    for (auto& kv : m.symtab) {
        if (!kv.first.empty() && kv.first[0] == '.') continue;
        emitSym(kv.first, kv.second);
    }
    f << "\n";

    for (auto& sectionName : m.sectionOrder) {
        auto sit = m.sections.find(sectionName);
        if (sit == m.sections.end()) continue;
        const ObjSectionData& sec = sit->second;

        f << "#SECTION " << sectionName << " size=" << std::dec << sec.data.size() << "\n";
        for (size_t i = 0; i < sec.data.size(); i++) {
            if (i % 16 == 0) {
                if (i) f << "\n";
                f << std::hex << std::right << std::setw(4) << std::setfill('0') << i << ": ";
            }
            f << std::hex << std::setw(2) << std::setfill('0') << (unsigned)sec.data[i] << " ";
        }
        if (!sec.data.empty()) f << "\n";
        f << std::dec << std::setfill(' ');

        if (!sec.relocs.empty()) {
            f << "#RELA " << sectionName << " count=" << std::dec << sec.relocs.size() << "\n"
              << std::left << std::setfill(' ')
              << std::setw(12) << "Offset"
              << std::setw(22) << "Symbol"
              << "Addend\n"
              << std::string(40, '-') << "\n";

            for (auto& r : sec.relocs) {
                f << "0x" << std::hex << std::right << std::setw(8) << std::setfill('0') << r.offset
                  << std::dec << std::setfill(' ')
                  << "  " << std::left << std::setw(22) << r.symbol
                  << r.addend << "\n";
            }
        }
        f << "\n";
    }
}

// ============================================================================
//  BINARNI FORMAT "SSOB"  (sve višebajtne vrednosti su little-endian)
//
//  [0]  Header (32 B)
//         0  4  magic = 'S','S','O','B'
//         4  2  version (=1)
//         6  2  endian marker (=1, little-endian)
//         8  4  sectionCount
//        12  4  symbolCount
//        16  4  relocCount
//        20  4  strtabOff   (apsolutni ofset u fajlu)
//        24  4  strtabSize
//        28  4  reserved (=0)
//  [32] Section header table: sectionCount * 12 B, u redosledu definisanja
//         0  4  nameOff      (ofset u string tabeli)
//         4  4  dataOff      (apsolutni ofset sirovih bajtova u fajlu)
//         8  4  dataSize
//       Symbol table: symbolCount * 16 B
//         0  4  nameOff
//         4  1  type   (0=UND, 1=LABEL, 2=SECTION)
//         5  1  bind   (0=LOCAL, 1=GLOBAL)
//         6  1  defined(0/1)
//         7  1  pad
//         8  4  sectionNameOff  (ofset imena sekcije simbola, npr. "UND")
//        12  4  value
//       Relocation table: relocCount * 16 B
//         0  4  sectionNameOff  (kojoj sekciji pripada zapis)
//         4  4  offset
//         8  4  symbolNameOff
//        12  4  addend (int32)
//       String table @ strtabOff: vodeći 00, pa NUL-terminisani nazivi
//       Section data blob: sirovi bajtovi sekcija (preko dataOff)
// ============================================================================

namespace {

void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
    b.push_back((uint8_t)((v >> 24) & 0xFF));
}

uint16_t get16(const std::vector<uint8_t>& b, size_t off) {
    return (uint16_t)(b[off] | (b[off + 1] << 8));
}
uint32_t get32(const std::vector<uint8_t>& b, size_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8)
         | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

struct StrTab {
    std::vector<uint8_t>            bytes{0};   // vodeći NUL
    std::map<std::string, uint32_t> seen{{"", 0}};

    uint32_t add(const std::string& s) {
        auto it = seen.find(s);
        if (it != seen.end()) return it->second;
        uint32_t off = (uint32_t)bytes.size();
        bytes.insert(bytes.end(), s.begin(), s.end());
        bytes.push_back(0);
        seen[s] = off;
        return off;
    }
};

uint8_t typeCode(SymbolType t) {
    switch (t) {
        case SymbolType::LABEL:   return objfmt::T_LABEL;
        case SymbolType::SECTION: return objfmt::T_SECTION;
        default:                  return objfmt::T_UND;
    }
}
SymbolType typeFromCode(uint8_t c) {
    switch (c) {
        case objfmt::T_LABEL:   return SymbolType::LABEL;
        case objfmt::T_SECTION: return SymbolType::SECTION;
        default:                return SymbolType::UND;
    }
}

std::vector<std::string> symbolOrder(const ObjectModel& m) {
    std::vector<std::string> order;
    for (auto& sn : m.sectionOrder) {
        std::string key = "." + sn;
        if (m.symtab.count(key)) order.push_back(key);
    }
    for (auto& kv : m.symtab) {
        if (!kv.first.empty() && kv.first[0] == '.') continue;
        order.push_back(kv.first);
    }
    return order;
}

} 

void objWriteBinary(std::ostream& os, const ObjectModel& m) {
    const uint32_t sectionCount = (uint32_t)m.sectionOrder.size();
    std::vector<std::string> symOrder = symbolOrder(m);
    const uint32_t symbolCount = (uint32_t)symOrder.size();

    uint32_t relocCount = 0;
    for (auto& sn : m.sectionOrder) {
        auto it = m.sections.find(sn);
        if (it != m.sections.end()) relocCount += (uint32_t)it->second.relocs.size();
    }

    StrTab strtab;
    std::vector<uint32_t> secNameOff(sectionCount);
    for (uint32_t i = 0; i < sectionCount; i++)
        secNameOff[i] = strtab.add(m.sectionOrder[i]);

    std::vector<uint32_t> symNameOff(symbolCount), symSecOff(symbolCount);
    for (uint32_t i = 0; i < symbolCount; i++) {
        symNameOff[i] = strtab.add(symOrder[i]);
        symSecOff[i]  = strtab.add(m.symtab.at(symOrder[i]).section);
    }

    const uint32_t secTabOff = objfmt::HEADER_SIZE;
    const uint32_t symTabOff = secTabOff + objfmt::SEC_ENT_SIZE * sectionCount;
    const uint32_t relTabOff = symTabOff + objfmt::SYM_ENT_SIZE * symbolCount;
    const uint32_t strtabOff = relTabOff + objfmt::REL_ENT_SIZE * relocCount;
    const uint32_t strtabSize = (uint32_t)strtab.bytes.size();
    const uint32_t dataBlobOff = strtabOff + strtabSize;

    std::vector<uint32_t> secDataOff(sectionCount), secDataSize(sectionCount);
    uint32_t running = dataBlobOff;
    for (uint32_t i = 0; i < sectionCount; i++) {
        const ObjSectionData& sec = m.sections.at(m.sectionOrder[i]);
        secDataOff[i]  = running;
        secDataSize[i] = (uint32_t)sec.data.size();
        running += secDataSize[i];
    }

    std::vector<uint8_t> out;

    out.insert(out.end(), objfmt::MAGIC, objfmt::MAGIC + 4);
    put16(out, objfmt::VERSION);
    put16(out, objfmt::ENDIAN_LE);
    put32(out, sectionCount);
    put32(out, symbolCount);
    put32(out, relocCount);
    put32(out, strtabOff);
    put32(out, strtabSize);
    put32(out, 0); 

    for (uint32_t i = 0; i < sectionCount; i++) {
        put32(out, secNameOff[i]);
        put32(out, secDataOff[i]);
        put32(out, secDataSize[i]);
    }

    for (uint32_t i = 0; i < symbolCount; i++) {
        const Symbol& s = m.symtab.at(symOrder[i]);
        put32(out, symNameOff[i]);
        out.push_back(typeCode(s.type));
        out.push_back(s.isGlobal  ? 1 : 0);
        out.push_back(s.isDefined ? 1 : 0);
        out.push_back(0); // pad
        put32(out, symSecOff[i]);
        put32(out, s.value);
    }

    for (auto& sn : m.sectionOrder) {
        auto it = m.sections.find(sn);
        if (it == m.sections.end()) continue;
        uint32_t sOff = strtab.add(sn); // već postoji -> vraća isti ofset
        for (auto& r : it->second.relocs) {
            put32(out, sOff);
            put32(out, r.offset);
            put32(out, strtab.add(r.symbol));
            put32(out, (uint32_t)r.addend);
        }
    }

    out.insert(out.end(), strtab.bytes.begin(), strtab.bytes.end());

    for (uint32_t i = 0; i < sectionCount; i++) {
        const ObjSectionData& sec = m.sections.at(m.sectionOrder[i]);
        out.insert(out.end(), sec.data.begin(), sec.data.end());
    }

    os.write(reinterpret_cast<const char*>(out.data()), (std::streamsize)out.size());
}

bool objReadBinary(std::istream& is, ObjectModel& m) {
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(is)),
                            std::istreambuf_iterator<char>());
    const uint64_t total = b.size();
    if (total < objfmt::HEADER_SIZE) return false;
    if (memcmp(b.data(), objfmt::MAGIC, 4) != 0) return false;

    if (get16(b, 4) != objfmt::VERSION)   return false;
    if (get16(b, 6) != objfmt::ENDIAN_LE) return false;

    const uint32_t sectionCount = get32(b, 8);
    const uint32_t symbolCount  = get32(b, 12);
    const uint32_t relocCount   = get32(b, 16);
    const uint32_t strtabOff    = get32(b, 20);
    const uint32_t strtabSize   = get32(b, 24);

    auto region = [&](uint64_t off, uint64_t len) -> bool {
        return off <= total && len <= total - off;  
    };

    if (!region(strtabOff, strtabSize)) return false;
    if (strtabSize == 0 || b[strtabOff + strtabSize - 1] != 0) return false;

    bool strOk = true;
    auto str = [&](uint32_t off) -> std::string {
        if (off >= strtabSize) { strOk = false; return std::string(); }
        uint64_t start = (uint64_t)strtabOff + off;
        uint64_t limit = (uint64_t)strtabOff + strtabSize;   
        uint64_t p = start;
        while (p < limit && b[p] != 0) p++;
        return std::string(reinterpret_cast<const char*>(&b[start]), (size_t)(p - start));
    };

    m.sectionOrder.clear();
    m.symtab.clear();
    m.sections.clear();

    const uint64_t secTabOff = objfmt::HEADER_SIZE;
    if (!region(secTabOff, (uint64_t)sectionCount * objfmt::SEC_ENT_SIZE)) return false;
    for (uint32_t i = 0; i < sectionCount; i++) {
        uint64_t e = secTabOff + (uint64_t)i * objfmt::SEC_ENT_SIZE;
        std::string name = str(get32(b, e));
        uint32_t dataOff  = get32(b, e + 4);
        uint32_t dataSize = get32(b, e + 8);
        if (!region(dataOff, dataSize)) return false;
        if (m.sections.count(name)) return false;            
        m.sectionOrder.push_back(name);
        ObjSectionData sd;
        sd.data.assign(b.begin() + dataOff, b.begin() + dataOff + dataSize);
        m.sections[name] = std::move(sd);
    }

    const uint64_t symTabOff = secTabOff + (uint64_t)sectionCount * objfmt::SEC_ENT_SIZE;
    if (!region(symTabOff, (uint64_t)symbolCount * objfmt::SYM_ENT_SIZE)) return false;
    for (uint32_t i = 0; i < symbolCount; i++) {
        uint64_t e = symTabOff + (uint64_t)i * objfmt::SYM_ENT_SIZE;
        std::string name = str(get32(b, e));
        uint8_t tcode = b[e + 4];
        if (tcode > objfmt::T_SECTION) return false;        
        Symbol s;
        s.type      = typeFromCode(tcode);
        s.isGlobal  = b[e + 5] != 0;
        s.isDefined = b[e + 6] != 0;
        s.section   = str(get32(b, e + 8));
        s.value     = get32(b, e + 12);
        if (m.symtab.count(name)) return false;             
        m.symtab[name] = s;
    }

    const uint64_t relTabOff = symTabOff + (uint64_t)symbolCount * objfmt::SYM_ENT_SIZE;
    if (!region(relTabOff, (uint64_t)relocCount * objfmt::REL_ENT_SIZE)) return false;
    for (uint32_t i = 0; i < relocCount; i++) {
        uint64_t e = relTabOff + (uint64_t)i * objfmt::REL_ENT_SIZE;
        std::string secName = str(get32(b, e));
        ObjReloc r;
        r.offset = get32(b, e + 4);
        r.symbol = str(get32(b, e + 8));
        r.addend = (int32_t)get32(b, e + 12);
        auto it = m.sections.find(secName);
        if (it == m.sections.end()) return false;            
        if ((uint64_t)r.offset + 4 > it->second.data.size()) return false;
        it->second.relocs.push_back(r);
    }

    if (!strOk) return false;   
    return true;
}
