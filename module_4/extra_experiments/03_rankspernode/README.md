che il report testa solo agli estremi. A 4 nodi si varia rank-per-nodo
in {1,2,4,8,16,32} con thread OpenMP complementari (rpn*T = 32). Il
candidato naturale a battere entrambi è 2 rank/nodo (uno per socket:
fan-out piccolo + località NUMA). Driver: threadlevel_bench (riusa la
pipeline consegnata in mpi_pipeline.hpp, livello funneled).
SBATCH --job-name=m4x_rpn
