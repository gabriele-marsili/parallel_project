CXX       = g++
CXXFLAGS  = -O3 -std=c++20 -Wall -Wextra -pthread
# Add -march=native only when compiling on the target node (not frontend)
INCLUDES  = -Iinclude

all: hashjoin_seq hashjoin_par

hashjoin_seq: src/hashjoin_seq.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) src/hashjoin_seq.cpp -o $@

hashjoin_par: src/hashjoin_parallel.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) src/hashjoin_parallel.cpp -o $@

# Thread sanitizer build (for debugging race conditions)
hashjoin_par_tsan: src/hashjoin_parallel.cpp include/*.hpp
	$(CXX) -O1 -g -std=c++20 -fsanitize=thread -pthread $(INCLUDES) src/hashjoin_parallel.cpp -o $@

clean:
	rm -f hashjoin_seq hashjoin_par hashjoin_par_tsan

.PHONY: all clean
