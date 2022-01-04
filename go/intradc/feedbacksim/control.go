package feedbacksim

import "math"

type DowngradeFracController struct {
	PropGain        float64 `json:"propGain"`
	IgnoreDiffBelow float64 `json:"ignoreDiffBelow"`
}

func (c DowngradeFracController) ReviseFromWantFracLOPRI(cur, setpoint float64) float64 {
	x := c.PropGain * (setpoint - cur)
	if math.Abs(x) < c.IgnoreDiffBelow {
		return 0
	}
	return x
}
