Memory benchmark
================
This contains script and tool set for updater to do memory usage benchmarks. The
idea is simple: We run updater in common way with Valgrind tool Massif.

Using
-----
Run script corresponding to bench and you should get in current working directory
a files with following names: `massif.out.BENCH.PID` where `BENCH` is name of used
bench and `PID` is pid. The most interesting massif is the one with lowest pid
because that is trace of whole program execution. The rest of those files are from
subprocesses.

To visualize massif files you can use Massif Visualizer (massif-visualizer).

Benches
-------

### Install transaction (installone.sh)
This simply installs package with `pkgtransaction`. This gives an idea on how
single givem enough size package influences memory usage of transaction.

### Install transaction (updateone.sh)
This installs package with `pkgupdate`. This gives an idea on how single givem
enough size package influences memory usage of whole execution.

### Full system install (fullrun.sh)
This simulates complete run of updater. It plans and installs multiple packages to
system.
