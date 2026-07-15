Esp.6 — la superlinearità apparente (eff ~1.22 a T=4 nel report) è un
artefatto della baseline: il binario OpenMP contiene il prefetch software,
hashjoin_seq no. Qui la baseline sequenziale riceve le stesse ottimizzazioni
e si rimisura il rapporto a T=1: se l'offset 1.44x si riduce a ~1, la
spiegazione è confermata.
