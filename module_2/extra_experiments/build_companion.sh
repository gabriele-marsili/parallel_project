#!/bin/bash
# Genera COMPANION_M2.pdf da COMPANION_M2.md con le figure ancorate al testo (niente float).
set -e
cd "$(dirname "$0")"
export PATH="/Library/TeX/texbin:$PATH"
# Sostituisco solo i glyph che Helvetica Neue non ha (visti al build): freccia, identico,
# ceiling, segno meno unicode, middle dot. Il resto (glyph matematici, apici) Helvetica lo rende.
python3 -c "
s=open('COMPANION_M2.md',encoding='utf-8').read()
for a,b in [('→','->'),('≡','='),('⌈','ceil('),('⌉',')'),('−','-'),('·','*'),('∝','proporzionale a'),('≫','>>'),('≪','<<')]:
    s=s.replace(a,b)
open('_companion_tmp.md','w',encoding='utf-8').write(s)
"
pandoc _companion_tmp.md -o COMPANION_M2.pdf --pdf-engine=xelatex \
  -H pandoc_header.tex \
  -V geometry:margin=2cm -V fontsize=10pt \
  -V mainfont="Helvetica Neue" -V monofont="Menlo" 2>&1 | grep -i "missing char" | sort -u || true
rm -f _companion_tmp.md
echo "OK -> COMPANION_M2.pdf"