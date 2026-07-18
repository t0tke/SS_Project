# referiše simbol koji niko ne definiše
.extern missing_sym
.section cons
    .word missing_sym
.end
