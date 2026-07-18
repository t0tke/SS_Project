# Prazno parče sekcije 's' sa globalnim bsym na samom kraju agregata.
# Ako se 's' završi na 0x100000000, baza ovog parčeta (baseAddr+offsetInMerged)
# bi bila 0x100000000 i obmotala se na 0 -> mora biti greška.
.global bsym
.section s
bsym:
.end
