package proc

import (
	"bufio"
	"fmt"
	"io/fs"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

func getStartEnd(fsys fs.FS, logs []string) (time.Time, time.Time, error) {
	var (
		startOK, endOK bool
		start, end     time.Time
		err            error
	)

	update := func(p string) {
		if err != nil {
			return
		}
		f, e := fsys.Open(p)
		if e != nil {
			err = e
			return
		}
		defer f.Close()
		s := bufio.NewScanner(f)
		for s.Scan() {
			t := s.Text()
			if idx := strings.Index(t, "start-time: "); idx >= 0 {
				tstamp := strings.Fields(t[idx+len("start-time: "):])[0]
				tval, e := time.Parse(time.RFC3339Nano, tstamp)
				if e != nil {
					err = e
					return
				}
				if startOK {
					if tval.Before(start) {
						start = tval
					}
				} else {
					startOK = true
					start = tval
				}
			}
			if idx := strings.Index(t, "end-time: "); idx >= 0 {
				tstamp := strings.Fields(t[idx+len("end-time: "):])[0]
				tval, e := time.Parse(time.RFC3339Nano, tstamp)
				if e != nil {
					err = e
					return
				}
				if endOK {
					if tval.After(end) {
						end = tval
					}
				} else {
					endOK = true
					end = tval
				}
			}
		}
	}

	for _, l := range logs {
		update(l)
	}

	return start, end, err
}

type TestLopriClientShardLog struct {
	Shard int
	Path  string
}

type TestLopriClientLogs struct {
	Client string
	Shards []TestLopriClientShardLog
}

type TestLopriInstanceLogs struct {
	Instance string
	Clients  []TestLopriClientLogs
}

var testLopriRegex = regexp.MustCompile(
	`(^|.*/)testlopri-([^-]+)-client-([^.]+).out(.shard.[0-9]+)?$`)

func regGlobFiles(fsys fs.FS, prog *regexp.Regexp) ([]string, error) {
	var all []string
	err := fs.WalkDir(fsys, ".", func(path string, d fs.DirEntry, err error) error {
		if d != nil && !d.IsDir() && prog.MatchString(path) {
			all = append(all, path)
		}
		return nil
	})
	return all, err
}

func GlobAndCollectTestLopri(fsys fs.FS) ([]TestLopriInstanceLogs, error) {
	all, err := regGlobFiles(fsys, testLopriRegex)
	if err != nil {
		return nil, fmt.Errorf("failed to walk: %w", err)
	}
	instances := make(map[string]map[string][]TestLopriClientShardLog)
	for _, p := range all {
		matches := testLopriRegex.FindStringSubmatch(p)
		if matches == nil {
			continue
		}
		inst := matches[2]
		client := matches[3]
		shard := 0
		if matches[4] != "" {
			shard, err = strconv.Atoi(strings.TrimPrefix(matches[4], ".shard."))
			if err != nil {
				panic(fmt.Errorf("got non-numeric shard num: %w", err))
			}
		}
		if instances[inst] == nil {
			instances[inst] = make(map[string][]TestLopriClientShardLog)
		}
		instances[inst][client] = append(instances[inst][client],
			TestLopriClientShardLog{Shard: shard, Path: p})
	}

	var ret []TestLopriInstanceLogs
	for inst, clientMap := range instances {
		instance := TestLopriInstanceLogs{Instance: inst}
		for client, shards := range clientMap {
			sort.Slice(shards, func(i, j int) bool {
				return shards[i].Shard < shards[j].Shard
			})
			last := -1
			for _, s := range shards {
				if s.Shard == last {
					return nil, fmt.Errorf("found duplicate shard %d with path %s",
						s.Shard, s.Path)
				}
				last = s.Shard
			}
			instance.Clients = append(instance.Clients, TestLopriClientLogs{
				Client: client,
				Shards: shards,
			})
		}
		sort.Slice(instance.Clients, func(i, j int) bool {
			return instance.Clients[i].Client < instance.Clients[j].Client
		})
		ret = append(ret, instance)
	}
	sort.Slice(ret, func(i, j int) bool {
		return ret[i].Instance < ret[j].Instance
	})
	return ret, nil
}

type FortioClientShardLog struct {
	Shard int
	Path  string
}

type FortioClientLogs struct {
	Client string
	Shards []FortioClientShardLog
}

type FortioInstanceLogs struct {
	Group    string
	Instance string
	Clients  []FortioClientLogs
}

var fortioRegex = regexp.MustCompile(
	`(^|.*/)fortio-([^-]+)-([^-]+)-client-([^.]+).out(.[0-9]+)?$`)

func GlobAndCollectFortio(fsys fs.FS) ([]FortioInstanceLogs, error) {
	all, err := regGlobFiles(fsys, fortioRegex)
	if err != nil {
		return nil, fmt.Errorf("failed to walk: %w", err)
	}

	type groupInst struct {
		g    string
		inst string
	}

	instances := make(map[groupInst]map[string][]FortioClientShardLog)
	for _, p := range all {
		matches := fortioRegex.FindStringSubmatch(p)
		if matches == nil {
			continue
		}
		group := matches[2]
		inst := matches[3]
		client := matches[4]
		shard := 0
		if matches[5] != "" {
			shard, err = strconv.Atoi(strings.TrimPrefix(matches[5], "."))
			if err != nil {
				panic(fmt.Errorf("got non-numeric shard num: %w", err))
			}
		}
		if instances[groupInst{group, inst}] == nil {
			instances[groupInst{group, inst}] = make(map[string][]FortioClientShardLog)
		}
		instances[groupInst{group, inst}][client] = append(instances[groupInst{group, inst}][client],
			FortioClientShardLog{Shard: shard, Path: p})
	}

	var ret []FortioInstanceLogs
	for inst, clientMap := range instances {
		instance := FortioInstanceLogs{Group: inst.g, Instance: inst.inst}
		for client, shards := range clientMap {
			sort.Slice(shards, func(i, j int) bool {
				return shards[i].Shard < shards[j].Shard
			})
			last := -1
			lastPath := ""
			for _, s := range shards {
				if s.Shard == last {
					return nil, fmt.Errorf("found duplicate shard %d with path %s (dup of %s)",
						s.Shard, s.Path, lastPath)
				}
				last = s.Shard
				lastPath = s.Path
			}
			instance.Clients = append(instance.Clients, FortioClientLogs{
				Client: client,
				Shards: shards,
			})
		}
		sort.Slice(instance.Clients, func(i, j int) bool {
			return instance.Clients[i].Client < instance.Clients[j].Client
		})
		ret = append(ret, instance)
	}
	sort.Slice(ret, func(i, j int) bool {
		return ret[i].Instance < ret[j].Instance
	})
	return ret, nil
}

func globToAlignPerHost(fsys fs.FS, r *regexp.Regexp) ([]NamedLog, error) {
	all, err := regGlobFiles(fsys, r)
	if err != nil {
		return nil, fmt.Errorf("failed to walk: %w", err)
	}
	var ret []NamedLog
	for _, p := range all {
		matches := r.FindStringSubmatch(p)
		if matches == nil {
			continue
		}
		node := matches[2] + parseSubnode(matches[3])
		ret = append(ret, NamedLog{
			Name: node,
			Path: p,
		})
	}
	sort.Slice(ret, func(i, j int) bool {
		return ret[i].Name < ret[j].Name
	})
	return ret, nil
}

var hostAgentStatsRegex = regexp.MustCompile(
	`(^|.*/)([^/]+)/logs(/[^/]+)?/host-agent-stats.log$`)

func GlobAndCollectHostAgentStats(fsys fs.FS) ([]NamedLog, error) {
	return globToAlignPerHost(fsys, hostAgentStatsRegex)
}

var hostAgentStatsFineGrainedRegex = regexp.MustCompile(
	`(^|.*/)([^/]+)/logs(/[^/]+)?/host-agent-fine-grained-stats.log$`)

func GlobAndCollectHostAgentStatsFineGrained(fsys fs.FS) ([]NamedLog, error) {
	return globToAlignPerHost(fsys, hostAgentStatsFineGrainedRegex)
}

var hostStatsRegex = regexp.MustCompile(
	`(^|.*/)([^/]+)/logs(/[^/]+)?/host-stats.log$`)

func GlobAndCollectHostStats(fsys fs.FS) ([]NamedLog, error) {
	return globToAlignPerHost(fsys, hostStatsRegex)
}

var hostEnforcerLogsRegex = regexp.MustCompile(
	`(^|.*/)([^/]+)/logs(/[^/]+)?/host-enforcer-debug`)

func GlobAndCollectHostEnforcerLogs(fsys fs.FS) ([]NamedLog, error) {
	files, err := globToAlignPerHost(fsys, hostEnforcerLogsRegex)
	if err != nil {
		return nil, err
	}

	var logDirs []NamedLog
	added := make(map[string]bool)
	for _, f := range files {
		i := strings.LastIndex(f.Path, "/host-enforcer-debug")
		d := f.Path[:i+len("/host-enforcer-debug")]
		if !added[d] {
			added[d] = true
			logDirs = append(logDirs, NamedLog{Name: f.Name, Path: d})
		}
	}

	return logDirs, nil
}
