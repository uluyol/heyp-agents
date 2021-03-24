package proc

import (
	"fmt"
	"io/fs"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

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

func getTestLopriFiles(fsys fs.FS, prog *regexp.Regexp) ([]string, error) {
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
	all, err := getTestLopriFiles(fsys, testLopriRegex)
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
