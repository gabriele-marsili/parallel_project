moduli 2/3 usavano 128). Vincolo strutturale: P multiplo dei rank. Si
misura sia l'ibrido (4 rank) sia il pure MPI (128 rank, quindi P>=128).
SBATCH --job-name=m4x_psweep
SBATCH --partition=normal
SBATCH --nodes=4
SBATCH --ntasks-per-node=32
