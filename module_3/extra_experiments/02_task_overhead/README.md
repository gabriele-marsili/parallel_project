Esp.2 — costo del modello a task sulle fasi regolari (histogram/scatter) e
necessità del nowait sul single. Il report attribuisce il gap loop-vs-task su
carico uniforme al dispatch dei task: qui lo si isola variando il numero di
task per fase, con il loop di riferimento sempre allo stesso T e sullo stesso
carico dei task (T in {8,16,32}, uniform e skew), e si misura il nowait con un
binario compilato senza (stesso sorgente, -DNO_NOWAIT).

Il sweep serve anche a misurare il jitter di partenza dei thread attraverso la
varianza: a un task per thread le fasi regolari oscillano del 25% fra le rep,
con 1024 task tornano stabili come il loop (0.4-1.2%).

results/repro.csv è un controllo di servizio (stessa config rimisurata in 8
invocazioni, metà precedute dal binario nonowait): serviva a escludere deriva
temporale ed effetto del processo precedente come cause della variabilità.
Entrambe escluse; la causa è nel modello a task, e si legge già dal sweep.
Nessuna figura, tenuto solo come tracciabilità.
