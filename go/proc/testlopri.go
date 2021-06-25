package proc

import (
	"fmt"
	"io/fs"
	"regexp"
	"time"
)

var testLopriLogsRegex = regexp.MustCompile(
	`(^|.*/)testlopri-.*-client-.*\.log$`)

func GetStartEndTestLopri(fsys fs.FS) (time.Time, time.Time, error) {
	logs, err := regGlobFiles(fsys, testLopriLogsRegex)
	if err != nil {
		return time.Time{}, time.Time{}, fmt.Errorf("failed to glob: %w", err)
	}

	return getStartEnd(fsys, logs)
}
