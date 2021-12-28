package filestat

import (
	"errors"
	"syscall"
	"testing"
)

func TestGetCurOwnersOutUIDAndGID_StatAlwaysFails(t *testing.T) {
	nextExpected := []string{
		"/path/to/X",
		"/path/to",
		"/path",
	}
	uid, gid := getCurOwnersOutUIDAndGIDImpl("/path/to/X", 2, 3, func(d string, s *syscall.Stat_t) error {
		t.Logf("stat %s", d)
		if d != nextExpected[0] {
			t.Errorf("called stat on %s, want %s", d, nextExpected[0])
		}
		nextExpected = nextExpected[1:]
		return errors.New("bad")
	})
	if uid != 2 {
		t.Errorf("bad uid: got %d want 2", uid)
	}
	if gid != 3 {
		t.Errorf("bad gid: got %d want 3", gid)
	}
}

func TestGetCurOwnersOutUIDAndGID_StatNeverFails(t *testing.T) {
	uid, gid := getCurOwnersOutUIDAndGIDImpl("/path/to/X", 2, 3, func(d string, s *syscall.Stat_t) error {
		t.Logf("stat %s", d)
		if d != "/path/to/X" {
			t.Errorf("called stat on %s, want /path/to/X", d)
		}
		*s = syscall.Stat_t{
			Uid: 4,
			Gid: 5,
		}
		return nil
	})
	if uid != 4 {
		t.Errorf("bad uid: got %d want 4", uid)
	}
	if gid != 5 {
		t.Errorf("bad gid: got %d want 5", gid)
	}
}

func TestGetCurOwnersOutUIDAndGID_StatOnceFails(t *testing.T) {
	timesCalled := 0
	nextExpected := []string{
		"/path/to/X",
		"/path/to",
		"/path",
	}
	uid, gid := getCurOwnersOutUIDAndGIDImpl("/path/to/X", 2, 3, func(d string, s *syscall.Stat_t) error {
		t.Logf("stat %s", d)
		if d != nextExpected[0] {
			t.Errorf("called stat on %s, want %s", d, nextExpected[0])
		}
		nextExpected = nextExpected[1:]
		timesCalled++
		if d == "/path/to" {
			*s = syscall.Stat_t{
				Uid: 6,
				Gid: 7,
			}
			return nil
		}
		return errors.New("bad")
	})
	if timesCalled != 2 {
		t.Errorf("stat called %d times, expected 2 times", timesCalled)
	}
	if uid != 6 {
		t.Errorf("bad uid: got %d want 6", uid)
	}
	if gid != 7 {
		t.Errorf("bad gid: got %d want 7", gid)
	}
}
