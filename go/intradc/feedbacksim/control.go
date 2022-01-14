package feedbacksim

import (
	"fmt"
	"log"
	"math"
)

type DowngradeFracController struct {
	// MaxInc is the maximium (absolute) fraction of traffic that
	// will be downgraded or upgraded in one control loop.
	MaxInc float64 `json:"maxInc"`

	// Add err * PropGain to the amount of traffic to downgrade
	PropGain float64 `json:"propGain"`

	// Don't downgrade/upgrade if err < IgnoreErrBelow
	IgnoreErrBelow float64 `json:"ignoreErrBelow"`

	// Don't downgrade/upgrade if err < IgnoreErrByCountMultiplier * 1 / # of flows
	IgnoreErrByCountMultiplier float64 `json:"ignoreErrByCountMultiplier"`
}

func (c DowngradeFracController) TrafficFracToDowngrade(cur, setpoint float64,
	inputToOutputConversion, maxTaskFlowFrac float64) float64 {
	err := cur - setpoint
	if 0 < err && err < c.IgnoreErrBelow {
		if debugController {
			log.Printf("controller: 0 [in noise]\n")
		}
		return 0
	} else if 0 < err && err < c.IgnoreErrByCountMultiplier*maxTaskFlowFrac {
		if debugController {
			log.Printf("controller: 0 [too coarse]\n")
		}
		return 0
	}
	x := c.PropGain * err
	x *= inputToOutputConversion
	if x < -1 || x > 1 {
		panic(fmt.Errorf("saw invalid pre-clamping downgrade frac inc = %g\n"+
			"cur = %g setpoint = %g i2o = %g max task = %g"+
			"controller = %+v", x, cur, setpoint, inputToOutputConversion, maxTaskFlowFrac, c))
	}
	x = math.Copysign(math.Min(math.Abs(x), c.MaxInc), x)
	if debugController {
		log.Printf("controller: %g [cur = %g setpoint = %g i2o = %g max task = %g]\n", x, cur, setpoint,
			inputToOutputConversion, maxTaskFlowFrac)
	}
	return x
}
