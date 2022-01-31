package proc

import (
	"fmt"
	"io/fs"
	"regexp"
	"time"
)

var hostAgentSimLogsRegex = regexp.MustCompile(
	`(^|.*/)host-agent-sim-?.*\.log$`)

func GetStartEndHostAgentSim(fsys fs.FS) (time.Time, time.Time, error) {
	logs, err := regGlobFiles(fsys, hostAgentSimLogsRegex)
	if err != nil {
		return time.Time{}, time.Time{}, fmt.Errorf("failed to glob: %w", err)
	}

	return getStartEnd(fsys, logs)
}
