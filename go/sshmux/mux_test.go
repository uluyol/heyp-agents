package sshmux_test

import (
	"bytes"
	"encoding/json"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/uluyol/heyp-agents/go/sshmux"
)

const jsonMux = `{
  "muxDir": "/tmp/XYZ",
  "destSockets": {
    "u@addr": "/tmp/XYZ/sock.1",
	"u2@addr2.com": "/tmp/XYZ/sock.2",
	"10.0.12.254": "/tmp/XYZ/sock.3",
	"z-1@10.0.12.254": "/tmp/XYZ/sock.4",
	"z32@10::254": "/tmp/XYZ/sock.5"
  },
  "counter": 5
}`

func TestMuxJSONUnmarshal(t *testing.T) {
	var d1 sshmux.Mux
	if err := json.Unmarshal([]byte(jsonMux), &d1); err != nil {
		t.Fatalf("failed to unmarshal d1: %v", err)
	}
	json2, err := d1.MarshalJSON()
	if err != nil {
		t.Fatalf("failed to marshal d1: %v", err)
	}
	var d2 sshmux.Mux
	if err := json.Unmarshal([]byte(json2), &d2); err != nil {
		t.Fatalf("failed to unmarshal d2: %v", err)
	}

	checkS := func(dst, want string) {
		t.Helper()
		if got1 := d1.S(dst); got1 != want {
			t.Errorf("d1.S(%s): got %s want %s", dst, got1, want)
		}
		if got2 := d2.S(dst); got2 != want {
			t.Errorf("d2.S(%s): got %s want %s", dst, got2, want)
		}
	}

	checkS("u@addr", "/tmp/XYZ/sock.1")
	checkS("u2@addr2.com", "/tmp/XYZ/sock.2")
	checkS("10.0.12.254", "/tmp/XYZ/sock.3")
	checkS("z-1@10.0.12.254", "/tmp/XYZ/sock.4")
	checkS("z32@10::254", "/tmp/XYZ/sock.5")
}

func TestMuxJSONRoundTrip(t *testing.T) {
	var d1 sshmux.Mux
	if err := json.Unmarshal([]byte(jsonMux), &d1); err != nil {
		t.Fatalf("failed to unmarshal d1: %v", err)
	}
	json2, err := d1.MarshalJSON()
	if err != nil {
		t.Fatalf("failed to marshal d1: %v", err)
	}
	var d2 sshmux.Mux
	if err := json.Unmarshal([]byte(json2), &d2); err != nil {
		t.Fatalf("failed to unmarshal d2: %v", err)
	}
	json3, err := d2.MarshalJSON()
	if err != nil {
		t.Fatalf("failed to marshal d2: %v", err)
	}
	if !bytes.Equal(json2, json3) {
		t.Errorf("did not roundtrip correctly:\nA = %s\nB = %s", json2, json3)
	}
}

func TestGenSSHWrapperFunc(t *testing.T) {
	const want = `#!/bin/bash

set -e

sockfile=""

for arg in "$@"; do
	if [[ $arg == 10.0.12.254 ]]; then
		sockfile=/tmp/XYZ/sock.3
		break
	fi
	if [[ $arg == u2@addr2.com ]]; then
		sockfile=/tmp/XYZ/sock.2
		break
	fi
	if [[ $arg == u@addr ]]; then
		sockfile=/tmp/XYZ/sock.1
		break
	fi
	if [[ $arg == z-1@10.0.12.254 ]]; then
		sockfile=/tmp/XYZ/sock.4
		break
	fi
	if [[ $arg == z32@10::254 ]]; then
		sockfile=/tmp/XYZ/sock.5
		break
	fi
done

if [[ -n $sockfile ]]; then
	exec /PATH/TO/ssh -S "$sockfile" "$@"
fi
exec /PATH/TO/ssh "$@"
`
	var d1 sshmux.Mux
	if err := json.Unmarshal([]byte(jsonMux), &d1); err != nil {
		t.Fatalf("failed to unmarshal d1: %v", err)
	}
	got, err := d1.GenSSHWraperFunc(func(s string) (string, error) { return "/PATH/TO/" + s, nil })
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if got != want {
		t.Errorf("got != want: diff (= got - want):\n%s", cmp.Diff(want, got))
	}
}
