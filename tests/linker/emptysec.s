# Prazna sekcija 'b' sa globalnim simbolom esym; relokacija u 'a' referiše esym.
# Ako se 'a' smesti da se završi na 0x100000000, 'b' (prazna) bi počela na
# 0x100000000 i esym bi se obmotao na 0 -> mora biti greška.
.global esym
.section b
esym:
.section a
    .word esym
.end
