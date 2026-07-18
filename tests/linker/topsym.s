# Globalni simbol tačno na kraju sekcije (ofset == veličina).
# Ako se sekcija smesti da se završava na 0x100000000, adresa ovog
# simbola bi se obmotala na 0 -> mora biti greška.
.global topend
.section s
    .word topend
topend:
.end
