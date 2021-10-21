package calg

/*
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

int64_t HeypKnapsackUsageLOPRI(int64_t* demands, size_t num_demands,
                               double want_frac_lopri, uint8_t* out_is_lopri);
int64_t HeypMaxMinFairWaterlevel(int64_t admission, int64_t* demands,
                                 size_t num_demands);

#cgo LDFLAGS: -static -L${SRCDIR} -lexport_bundle -lc++ -lc++abi -pthread -lm -lpthread -lz -ldl

*/
import "C"
import "unsafe"

func KnapsackUsageLOPRI(demands []int64, wantFracLOPRI float64) ([]int, int64) {
	if wantFracLOPRI == 0 {
		return nil, 0
	}

	demandsPtr := (*C.int64_t)(unsafe.Pointer(&demands))
	isLOPRI := (*C.uint8_t)(C.malloc(C.size_t(len(demands))))
	lopriUsage := int64(C.HeypKnapsackUsageLOPRI(demandsPtr, C.size_t(len(demands)),
		C.double(wantFracLOPRI), isLOPRI))

	var lopri []int
	isLOPRISlice := unsafe.Slice(isLOPRI, len(demands))
	for id, picked := range isLOPRISlice {
		if picked != 0 {
			lopri = append(lopri, id)
		}
	}
	C.free(unsafe.Pointer(isLOPRI))

	return lopri, lopriUsage
}

func MaxMinFairWaterlevel(demands []int64, admission int64) int64 {
	demandsPtr := (*C.int64_t)(unsafe.Pointer(&demands))
	return int64(C.HeypMaxMinFairWaterlevel(C.int64_t(admission), demandsPtr,
		C.size_t(len(demands))))
}
