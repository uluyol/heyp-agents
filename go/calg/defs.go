package calg

/*
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

int64_t HeypKnapsackUsageLOPRI(int64_t* demands, size_t num_demands,
                               double want_frac_lopri, uint8_t* out_is_lopri);
int64_t HeypMaxMinFairWaterlevel(int64_t admission, int64_t* demands,
                                 size_t num_demands);

typedef struct HeypSelectLOPRIHashingCtx HeypSelectLOPRIHashingCtx;

HeypSelectLOPRIHashingCtx* NewHeypSelectLOPRIHashingCtx(uint64_t* child_ids,
                                                        size_t num_children);

void FreeHeypSelectLOPRIHashingCtx(HeypSelectLOPRIHashingCtx* ctx);

void HeypSelectLOPRIHashing(HeypSelectLOPRIHashingCtx* ctx, double want_frac_lopri,
                            uint8_t* use_lopris);

#cgo LDFLAGS: -static -L${SRCDIR} -lexport_bundle -lc++ -lc++abi -pthread -lm -lpthread -lz -ldl

*/
import "C"
import (
	"unsafe"

	"github.com/RoaringBitmap/roaring"
)

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

type HashingDowngradeSelector struct {
	c   *C.HeypSelectLOPRIHashingCtx
	out []C.uint8_t
}

func NewHashingDowngradeSelector(childIDs []uint64) HashingDowngradeSelector {
	cChildIDs := make([]C.uint64_t, len(childIDs))
	for i := range childIDs {
		cChildIDs[i] = C.uint64_t(childIDs[i])
	}
	c := C.NewHeypSelectLOPRIHashingCtx(&cChildIDs[0], C.size_t(len(childIDs)))
	return HashingDowngradeSelector{c, make([]C.uint8_t, len(childIDs))}
}

func (ctx HashingDowngradeSelector) Free() {
	C.FreeHeypSelectLOPRIHashingCtx(ctx.c)
	ctx.c = nil
}

func (ctx HashingDowngradeSelector) PickLOPRI(wantFracLOPRI float64, isLOPRI *roaring.Bitmap) {
	if ctx.c == nil {
		panic("nil HashingDowngradeSelector")
	}
	C.HeypSelectLOPRIHashing(ctx.c, C.double(wantFracLOPRI), &ctx.out[0])
	for i := range ctx.out {
		if ctx.out[i] != 0 {
			isLOPRI.AddInt(i)
		}
	}
}
