package logs

import (
	"archive/zip"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

type DataFS struct {
	mu         sync.Mutex
	root       logsRootData
	rawEntries map[string]*logsEntry
	zip        map[string]*zip.ReadCloser
	err        error
}

type logsRoot struct {
	mu sync.Mutex
	logsRootData
}

type logsRootData struct {
	topdir  string
	entries []fs.DirEntry
}

func (f *logsRoot) Stat() (fs.FileInfo, error) { return fs.Stat(os.DirFS(f.topdir), ".") }
func (f *logsRoot) Read([]byte) (int, error)   { return 0, errors.New("reading root dir") }
func (f *logsRoot) Close() error               { return nil }

func (f *logsRoot) ReadDir(n int) ([]fs.DirEntry, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if n > 0 && n < len(f.entries) {
		toret := f.entries[:n]
		f.entries = f.entries[n:]
		return toret, nil
	}
	toret := f.entries
	f.entries = nil
	return toret, nil
}

var _ fs.ReadDirFile = new(logsRoot)

type logsEntry struct {
	name    string
	mode    fs.FileMode
	size    int64
	modTime time.Time
	isZip   bool
}

func (e *logsEntry) Name() string       { return e.name }
func (e *logsEntry) IsDir() bool        { return e.mode.IsDir() }
func (e *logsEntry) Type() fs.FileMode  { return e.mode }
func (e *logsEntry) Mode() fs.FileMode  { return e.mode }
func (e *logsEntry) ModTime() time.Time { return e.modTime }
func (e *logsEntry) Size() int64        { return e.size }
func (e *logsEntry) Sys() interface{}   { return nil }

func (e *logsEntry) Info() (fs.FileInfo, error) { return e, nil }

var _ fs.DirEntry = new(logsEntry)
var _ fs.FileInfo = new(logsEntry)

func NewDataFS(topdir string) (*DataFS, error) {
	dirEnt, err := os.ReadDir(topdir)
	if err != nil {
		return nil, fmt.Errorf("failed to read top dir: %w", err)
	}

	fsys := &DataFS{
		root: logsRootData{
			topdir:  topdir,
			entries: make([]fs.DirEntry, len(dirEnt)),
		},
		rawEntries: make(map[string]*logsEntry, len(dirEnt)),
		zip:        make(map[string]*zip.ReadCloser, len(dirEnt)),
	}

	for i, de := range dirEnt {
		info, err := de.Info()
		if err != nil {
			return nil, fmt.Errorf("failed to get FileInfo: %w", err)
		}
		re := new(logsEntry)
		re.name = info.Name()
		re.mode = info.Mode()
		re.size = info.Size()
		re.modTime = info.ModTime()
		if !info.IsDir() && strings.HasSuffix(info.Name(), ".zip") {
			re.name = strings.TrimSuffix(info.Name(), ".zip")
			re.mode |= fs.ModeDir
			re.isZip = true
		}

		fsys.root.entries[i] = re

		if _, ok := fsys.rawEntries[re.name]; ok {
			return nil, fmt.Errorf("got duplicate direntry %s: ", re.name)
		}
		fsys.rawEntries[re.name] = re
	}

	return fsys, nil
}

func stringsCut(s, sep string) (before, after string, ok bool) {
	if i := strings.Index(s, sep); i >= 0 {
		return s[:i], s[i+len(sep):], true
	}
	return s, "", false
}

func (fsys *DataFS) Open(name string) (fs.File, error) {
	if strings.Count(name, "/") > 30 {
		return nil, fmt.Errorf("exceeded recursion limit with %s", name)
	}

	fsys.mu.Lock()
	defer fsys.mu.Unlock()

	if name == "." {
		t := new(logsRoot)
		t.logsRootData = fsys.root
		return t, nil
	}

	name, sub, hasSub := stringsCut(name, "/")
	if hasSub {
		ent := fsys.rawEntries[name]
		if ent == nil {
			return nil, fmt.Errorf("not found: %s", name)
		}

		if ent.isZip {
			fsys2, err := fsys.openZip(name)
			if err != nil {
				return nil, err
			}
			return fsys2.Open(sub)
		} else {
			fsys2 := os.DirFS(filepath.Join(fsys.root.topdir, name))
			return fsys2.Open(sub)
		}
	}

	ent := fsys.rawEntries[name]
	if ent == nil {
		return nil, fmt.Errorf("not found: %s", name)
	}

	if ent.isZip {
		fsys2, err := fsys.openZip(name)
		if err != nil {
			return nil, err
		}
		rootf, err := fsys2.Open(".")
		return rootf, err
	} else {
		f, err := os.Open(filepath.Join(fsys.root.topdir, name))
		return f, err
	}
}

// must hold fsys.mu
func (fsys *DataFS) openZip(name string) (fs.FS, error) {
	zipf := fsys.zip[name]
	if zipf == nil {
		var err error
		zipf, err = zip.OpenReader(filepath.Join(fsys.root.topdir, name+".zip"))
		if err != nil {
			return nil, err
		}
		fsys.zip[name] = zipf
	}
	return &zipf.Reader, nil
}

func (fsys *DataFS) Close() error {
	fsys.mu.Lock()
	defer fsys.mu.Unlock()

	setErr := func(e error) {
		if e != nil {
			if fsys.err == nil {
				fsys.err = e
			}
		}
	}

	for k, rc := range fsys.zip {
		setErr(rc.Close())
		delete(fsys.zip, k)
	}

	return fsys.err
}
