package calg

/*
#include <stdint.h>
#include <stddef.h>

int64_t HeypKnapsackUsageLOPRI(int64_t* demands, size_t num_demands,
                               double want_frac_lopri);
int64_t HeypMaxMinFairWaterlevel(int64_t admission, int64_t* demands,
                                 size_t num_demands);

#cgo LDFLAGS: -static -L${SRCDIR} -lexport_bundle -lc++ -lc++abi -pthread -lm -lpthread -lz -ldl

*/
import "C"
import "unsafe"

func KnapsackUsageLOPRI(demands []int64, want_frac_lopri float64) int64 {
	demands_ptr := (*C.int64_t)(unsafe.Pointer(&demands))
	return int64(C.HeypKnapsackUsageLOPRI(demands_ptr, C.size_t(len(demands)),
		C.double(want_frac_lopri)))
}

func MaxMinFairWaterlevel(demands []int64, admission int64) int64 {
	demands_ptr := (*C.int64_t)(unsafe.Pointer(&demands))
	return int64(C.HeypMaxMinFairWaterlevel(C.int64_t(admission), demands_ptr,
		C.size_t(len(demands))))
}
