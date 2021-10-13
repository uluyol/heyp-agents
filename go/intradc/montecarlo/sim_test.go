package montecarlo

import "testing"

func TestMetricOne(t *testing.T) {
	var m metric
	m.Record(65)
	assertEq(t, m.Mean(), 65, "Mean")
	d := m.DistPercs()
	assertEq(t, d.P0, 65, "P0")
	assertEq(t, d.P5, 65, "P5")
	assertEq(t, d.P10, 65, "P10")
	assertEq(t, d.P50, 65, "P50")
	assertEq(t, d.P90, 65, "P90")
	assertEq(t, d.P95, 65, "P95")
	assertEq(t, d.P100, 65, "P100")
}

func TestMetricTwenty(t *testing.T) {
	var m metric
	// write out of order to test m percentiles
	vals := []float64{
		9, -10,
		0, 1, 2, 3, 4, 5, 6, 7, 8,
		-9, -8, -7, -6, -5, -4, -3, -2, -1,
	}
	for _, v := range vals {
		m.Record(v)
	}
	assertEq(t, m.Mean(), -0.5, "Mean")
	d := m.DistPercs()
	assertEq(t, d.P0, -10, "P0")
	assertEq(t, d.P5, -9, "P5")
	assertEq(t, d.P10, -8, "P10")
	assertEq(t, d.P50, 0, "P50")
	assertEq(t, d.P90, 7, "P90")
	assertEq(t, d.P95, 8, "P95")
	assertEq(t, d.P100, 9, "P100")
}

func assertEq(t *testing.T, got, want float64, name string) {
	if got != want {
		t.Errorf("for %s, got %g, want %g", name, got, want)
	}
}
