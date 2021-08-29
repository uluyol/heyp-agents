package proc

import (
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
)

func fsToFilepath(s string) string {
	var fields []string
	for s != "" {
		s = path.Clean(s)
		var f string
		s, f = path.Split(s)
		if f != "" {
			fields = append([]string{f}, fields...)
		}
	}
	return filepath.Join(fields...)
}

var testConfigFilesRegex = regexp.MustCompile(`(^|.*/)configs/.*$`)

func extractConfigs(fsys fs.FS, dest string) error {
	cfgFiles, err := regGlobFiles(fsys, testConfigFilesRegex)
	if err != nil {
		return fmt.Errorf("failed to glob config files: %w", err)
	}

	for _, f := range cfgFiles {
		fdir, fbase := path.Split(f)

		d := filepath.Join(dest, fsToFilepath(fdir))
		if err := os.MkdirAll(d, 0o755); err != nil {
			return fmt.Errorf("failed to mkdir %s: %w", d, err)
		}

		fout, err := os.Create(filepath.Join(d, fbase))
		if err != nil {
			return fmt.Errorf("failed to create file: %v", err)
		}

		fin, err := fsys.Open(f)
		if err != nil {
			return fmt.Errorf("failed to open file: %v", err)
		}

		_, err = io.Copy(fout, fin)

		closeErr := fout.Close()
		fin.Close()

		if err == nil {
			err = closeErr
		}

		if err != nil {
			return fmt.Errorf("failed to write file: %v", err)
		}
	}
	return nil
}

func DiffRemoteConfigs(aFS, bFS fs.FS, w io.Writer) error {
	tmpdir, err := os.MkdirTemp("", "proc-heyp.")
	if err != nil {
		return fmt.Errorf("failed to glob config files: %w", err)
	}
	defer os.RemoveAll(tmpdir)

	extractConfigs(aFS, filepath.Join(tmpdir, "X"))

	cmd := exec.Command("git", "init")
	cmd.Dir = tmpdir
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to init diff repo: %v; output: %s\n", err, out)
	}

	cmd = exec.Command("git", "add", "X")
	cmd.Dir = tmpdir
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add initial files to diff repo: %v; output: %s\n", err, out)
	}

	cmd = exec.Command("git", "commit", "-m", "add a")
	cmd.Dir = tmpdir
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to checkin initial files to diff repo: %v; output: %s\n", err, out)
	}

	cmd = exec.Command("rm", "-r", filepath.Join(tmpdir, "X"))
	cmd.Dir = tmpdir
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to clear diff repo: %v; output: %s\n", err, out)
	}

	extractConfigs(bFS, filepath.Join(tmpdir, "X"))

	cmd = exec.Command("git", "add", ".")
	cmd.Dir = tmpdir
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to add initial files to diff repo: %v; output: %s\n", err, out)
	}

	cmd = exec.Command("git", "diff", "-M3", "HEAD")
	cmd.Dir = tmpdir
	cmd.Stdout = w

	return cmd.Run()
}
