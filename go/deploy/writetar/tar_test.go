package writetar

import (
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

type ArchiveWriter interface {
	Add(Input)
	AddAll([]Input)
	Close() error
}

func testArchive(t *testing.T, outname string, mkWriter func(string) (ArchiveWriter, error), untarArgs string) {
	t.Helper()

	name, err := ioutil.TempDir("", "archivewriter-test")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(name)

	tarPath := filepath.Join(name, outname)

	w, err := mkWriter(tarPath)
	if err != nil {
		t.Fatalf("failed to create writer: %v", err)
	}

	w.Add(Input{
		Dest:      "data/a",
		InputPath: "testdata/a",
		Mode:      0o644,
	})

	w.Add(Input{
		Dest:      "data/b",
		InputPath: "testdata/b",
		Mode:      0o644,
	})

	w.Add(Input{
		Dest:      "code",
		InputPath: "testdata/c/data",
		Mode:      0o644,
	})

	err = w.Close()
	if err != nil {
		t.Fatalf("failure writing %s: %v", tarPath, err)
	}

	cmd := exec.Command("tar", untarArgs, tarPath)
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

func TestXZWriter(t *testing.T) {
	testArchive(t, "data.tar.xz", func(p string) (ArchiveWriter, error) {
		w, e := NewXZWriter(p)
		return w, e
	}, "xJf")
}

func TestGzipWriter(t *testing.T) {
	testArchive(t, "data.tar.gz", func(p string) (ArchiveWriter, error) {
		w, e := NewGzipWriter(p)
		return w, e
	}, "xzf")
}
