package logs

import (
	"reflect"
	"strings"
	"testing"
)

func TestReadHTBClasses(t *testing.T) {
	tests := []struct {
		input string
		want  map[TCHandle]HTBClass
	}{
		{input: "", want: map[TCHandle]HTBClass{}},
		{input: "\n", want: map[TCHandle]HTBClass{}},
		{input: "\n  \n", want: map[TCHandle]HTBClass{}},
		{
			input: `class htb 1:2 root prio 0 rate 354917Kbit ceil 354917Kbit burst 1552b cburst 1552b`,
			want: map[TCHandle]HTBClass{
				TCHandle{1, 2}: HTBClass{TCHandle{1, 2}, 363_435_008, 363_435_008, 1552, 1552},
			},
		},
		{
			input: `
	class htb 1:2 root prio 0 rate 354917Kbit ceil 354917Kbit burst 1552b cburst 1552b
`,
			want: map[TCHandle]HTBClass{
				TCHandle{1, 2}: HTBClass{TCHandle{1, 2}, 363_435_008, 363_435_008, 1552, 1552},
			},
		},
		{
			input: `
class htb 1:2 root prio 0 rate 354917000 ceil 354917Kbit burst 1552b cburst 1552b
class htb 1:5 root prio 0 rate 354917mbit ceil 1Gbit burst 1552 cburst 2552k
`,
			want: map[TCHandle]HTBClass{
				TCHandle{1, 2}: HTBClass{TCHandle{1, 2}, 354_917_000, 363_435_008, 1552, 1552},
				TCHandle{1, 5}: HTBClass{TCHandle{1, 5}, 372_157_448_192, 1_073_741_824, 1552, 2_613_248},
			},
		},
	}

	for _, test := range tests {
		classes, err := ReadHTBClasses(strings.NewReader(test.input))
		if err != nil {
			t.Errorf("input %q\nunexpected error: %v", test.input, err)
		} else {
			if !reflect.DeepEqual(classes, test.want) {
				t.Errorf("input %q\ngot %v, want %v", test.input, classes, test.want)
			}
		}
	}
}
