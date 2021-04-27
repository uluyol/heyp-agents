package proc

import (
	"bytes"
	"testing"
)

func TestSortedPrint(t *testing.T) {
	testCases := []struct {
		inputs []string
		want   string
	}{
		{
			inputs: []string{
				"a\nb\nc\n",
				"d\ne\nf",
			},
			want: "a\nb\nc\nd\ne\nf",
		},
		{
			inputs: []string{
				"a\nb\nd\n",
				"a\nc\n",
			},
			want: "a\na\nb\nc\nd\n",
		},
		{
			inputs: []string{
				"a\nb\nd\nz\ne\n",
				"y\nc\nf\n",
			},
			want: "a\nb\nd\ny\nc\nf\nz\ne\n",
		},
	}

	var out bytes.Buffer
	for i, test := range testCases {
		inputs := make([][]byte, len(test.inputs))
		for j, s := range test.inputs {
			inputs[j] = []byte(s)
		}
		out.Reset()
		SortedPrint(&out, inputs)
		if got := out.String(); got != test.want {
			t.Errorf("test %d: got %s want %s", i, got, test.want)
		}
	}
}
