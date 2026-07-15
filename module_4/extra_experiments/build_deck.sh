#!/bin/bash
# Genera DECK_M4.pdf (deck di supporto orale, 1 grafico per pagina) da DECK_M4.md.
set -e
cd "$(dirname "$0")"
export PATH="/Library/TeX/texbin:$PATH"
SRC=esperimenti_aggiuntivi_M4.md
OUT=esperimenti_aggiuntivi_M4.pdf
# Stessa sanitizzazione dei companion: sostituisce i glyph che Helvetica Neue non ha.
python3 -c "
s=open('$SRC',encoding='utf-8').read()
for a,b in [('→','->'),('≡','='),('⌈','ceil('),('⌉',')'),('−','-'),('·','*'),('∝','proporzionale a'),('≫','>>'),('≪','<<')]:
    s=s.replace(a,b)
open('_deck_tmp.md','w',encoding='utf-8').write(s)
"
pandoc _deck_tmp.md -o "$OUT" --pdf-engine=xelatex \
  -H deck_header.tex \
  -V geometry:margin=2cm -V fontsize=11pt \
  -V mainfont="Helvetica Neue" -V monofont="Menlo" 2>&1 | grep -i "missing char" | sort -u || true
rm -f _deck_tmp.md
echo "OK -> $OUT"
