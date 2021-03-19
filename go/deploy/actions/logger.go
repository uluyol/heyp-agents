package actions

import "log"

func LogWithPrefix(prefix string) func(format string, args ...interface{}) {
	return func(format string, args ...interface{}) {
		log.Printf(prefix+format, args...)
	}
}
