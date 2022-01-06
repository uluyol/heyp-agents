package feedbacksim

import "math"

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
	inputToOutputConversion float64, numFlows int) float64 {
	err := cur - setpoint
	if 0 < err && err < c.IgnoreErrBelow {
		return 0
	} else if 0 < err && err < c.IgnoreErrByCountMultiplier/float64(numFlows) {
		return 0
	}
	x := c.PropGain * err
	x *= inputToOutputConversion
	x = math.Copysign(math.Min(math.Abs(x), c.MaxInc), x)
	return x
}
