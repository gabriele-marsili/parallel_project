Esp.2 — costo del modello a task sulle fasi regolari (histogram/scatter) e
necessità del nowait sul single. Il report attribuisce il gap loop-vs-task
(fino a 31% a T=16 uniforme) al dispatch dei task: qui lo si isola variando
il numero di task per fase a T fisso, e si misura il nowait con un binario
compilato senza (stesso sorgente, -DNO_NOWAIT).
