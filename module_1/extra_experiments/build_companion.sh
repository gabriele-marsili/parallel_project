#!/bin/bash
# Genera COMPANION_M1.pdf da COMPANION_M1.md con le figure ancorate al testo (niente float).
set -e
cd "$(dirname "$0")"
export PATH="/Library/TeX/texbin:$PATH"
# xelatex non ha la freccia unicode -> la sostituisco con "->" in una copia temporanea
python3 -c "s=open('COMPANION_M1.md',encoding='utf-8').read(); open('_companion_tmp.md','w',encoding='utf-8').write(s.replace('→','->'))"
pandoc _companion_tmp.md -o COMPANION_M1.pdf --pdf-engine=xelatex \
  -H pandoc_header.tex \
  -V geometry:margin=2cm -V fontsize=10pt \
  -V mainfont="Helvetica Neue" -V monofont="Menlo"
rm -f _companion_tmp.md
echo "OK -> COMPANION_M1.pdf"
