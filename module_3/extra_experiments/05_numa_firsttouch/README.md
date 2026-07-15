Esp.5 — NUMA first-touch dei buffer di partizione. Il codice consegnato fa
la prima scrittura in parallel for schedule(static) così ogni pagina finisce
sul nodo NUMA del thread che poi vi scrive nello scatter. Ablation: first
touch sequenziale (tutte le pagine sul nodo del master) e, se numactl è
disponibile, policy interleave.
