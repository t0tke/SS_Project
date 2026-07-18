# referiše 'shared' BEZ .extern -> nedefinisan LOKALAN simbol.
# Ne sme ga razrešiti global iz drugog fajla.
.section c
    ld shared, %r1
.end
