package writetar

import (
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestXZWriter(t *testing.T) {
	name, err := ioutil.TempDir("", "xzwriter-test")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(name)

	tarPath := filepath.Join(name, "data.tar.xz")

	w, err := NewXZWriter(tarPath)
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}

	w.Add(Input{
		Dest:      "data/a",
		InputPath: "testdata/a",
	})

	w.Add(Input{
		Dest:      "data/b",
		InputPath: "testdata/b",
	})

	w.Add(Input{
		Dest:      "code",
		InputPath: "testdata/c/data",
	})

	err = w.Close()
	if err != nil {
		t.Fatalf("failure writing %s: %v", tarPath, err)
	}

	cmd := exec.Command("tar", "xJf", tarPath)
	cmd.Dir = name
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("failed to extract %s: %v. output:\n%s", tarPath, err, out)
	}

	check := func(path, contents string) {
		t.Helper()

		data, err := ioutil.ReadFile(filepath.Join(name, path))
		if err == nil && string(data) != contents {
			t.Errorf("%s: have %q want %q", path, data, contents)
		}
		if err != nil {
			t.Errorf("%s: failed to read: %v", path, err)
		}
	}

	check("data/a", "ab\n")
	check("data/b", "Z\n")
	check("code", "FFF\n")
}
