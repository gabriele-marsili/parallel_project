ping-pong, inter-nodo e intra-nodo. Il report usa il modello di Hockney
(R-1)*alpha + beta*V per spiegare il weak scaling senza aver misurato i
coefficienti: qui si misurano, e il fit avviene nel plot.
SBATCH --job-name=m4x_hockney
SBATCH --partition=normal
SBATCH --nodes=2
