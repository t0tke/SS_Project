# Dve sekcije: ako se prva smesti da se završi na 0x100000000,
# naredna (auto) sekcija bi dobila bazu 0 -> mora biti greška.
.section a
    .skip 4
.section b
    .word 0
.end
