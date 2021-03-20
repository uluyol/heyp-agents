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
	`.*testlopri-([^-]+)-client-([^.]+).out(.shard.[0-9]+)?$`)

func GlobAndCollectTestLopri(fsys fs.FS) ([]TestLopriInstanceLogs, error) {
	all, err := fs.Glob(fsys, "testlopri-*client-*.out*")
	if err != nil {
		return nil, fmt.Errorf("failed to glob: %w", err)
	}
	instances := make(map[string]map[string][]TestLopriClientShardLog)
	for _, p := range all {
		matches := testLopriRegex.FindStringSubmatch(p)
		if matches == nil {
			continue
		}
		inst := matches[1]
		client := matches[2]
		shard := 0
		if matches[3] != "" {
			shard, err = strconv.Atoi(strings.TrimPrefix(matches[3], ".shard."))
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
