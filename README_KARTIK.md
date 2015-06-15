This version of zsim is modified for research into properties of cache.
More specifically, we compare per-line cache clusion property with the
overall cache clusion property that is typically implemented for most caches,
i.e. inclusive, exclusive or non-inclusive.

Scripts :

1. run_spec.sh -- this script runs all spec benchmarks using zsim.
   It takes arguments using the sim.cfg file which specifies the properties
   of the simulator.

2. run_parsec.sh -- this script runs all the parsec benchmarks, or
   the particular one specified using the sim.cfg file which specifies the
   architecture configuration.

3. run_graph500.sh benchmarks -- runs the graph500 benchmarks

The outputs of these benchmarks is a set of metrics that result from the
simulation, including power metrics that are obtained from McPat.
