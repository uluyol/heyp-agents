package host

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestIPTablesBaseRulesTmpl(t *testing.T) {
	tests := []struct {
		devs []string
		want string
	}{
		{
			nil, `
*nat
COMMIT
*filter
-A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
COMMIT
`,
		},
		{
			[]string{"eno49", "eno50"}, `
*nat
-A POSTROUTING -o eno49 -j MASQUERADE
-A POSTROUTING -o eno50 -j MASQUERADE
COMMIT
*filter
-A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
COMMIT
`,
		},
	}

	for _, tc := range tests {
		var sb strings.Builder
		if err := iptablesBaseRulesTmpl.Execute(&sb, tc.devs); err != nil {
			t.Fatalf("failed to execute template: %v", err)
		}
		if sb.String() != tc.want {
			t.Errorf("want - got: %v", cmp.Diff(tc.want, sb.String()))
		}
	}
}
