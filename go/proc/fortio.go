package proc

import (
	"fmt"
	"io/fs"
	"regexp"
	"time"
)

var fortioLogsRegex = regexp.MustCompile(
	`(^|.*/)fortio-.*-client-.*\.log$`)

func GetStartEndFortio(fsys fs.FS) (time.Time, time.Time, error) {
	logs, err := regGlobFiles(fsys, fortioLogsRegex)
	if err != nil {
		return time.Time{}, time.Time{}, fmt.Errorf("failed to glob: %w", err)
	}

	return getStartEnd(fsys, logs)
}
