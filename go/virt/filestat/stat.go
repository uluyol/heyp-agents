package filestat

import (
	"os"
	"path/filepath"
	"syscall"
)

func GetCurOwnersOutUIDAndGID(d string) (uid int, gid int) {
	return getCurOwnersOutUIDAndGIDImpl(d,
		os.Getuid(), os.Getgid(), syscall.Stat)
}

func getCurOwnersOutUIDAndGIDImpl(d string, uid, gid int,
	statFunc func(string, *syscall.Stat_t) error) (int, int) {
	var stat syscall.Stat_t
	for ; d != "" && d != "/"; d = filepath.Dir(d) {
		if statFunc(d, &stat) == nil {
			uid = int(stat.Uid)
			gid = int(stat.Gid)
			break
		}
	}
	return uid, gid
}
