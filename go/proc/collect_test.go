package proc

import (
	"reflect"
	"testing"
	"testing/fstest"
	"time"
)

func emptyFile() *fstest.MapFile {
	return &fstest.MapFile{
		Data:    []byte{0x0},
		Mode:    0644,
		ModTime: time.Unix(0, 0),
	}
}

func TestGlobAndCollectTestLopri(t *testing.T) {
	fsys := fstest.MapFS{
		"testlopri-green-client-node-1.out.shard.0":  emptyFile(),
		"testlopri-green-client-node-1.out.shard.3":  emptyFile(),
		"testlopri-green-client-node-1.out.shard.1":  emptyFile(),
		"testlopri-green-client-node-2.out":          emptyFile(), // no shard
		"testlopri-green-client-node-3.out":          emptyFile(), // no shard
		"testlopri-red-client-node-5.out.shard.1":    emptyFile(),
		"testlopri-red-client-node-5.out.shard.0":    emptyFile(),
		"testlopri-red-client-node6.out.shard.1":     emptyFile(),
		"testlopri-purple-client-node-1.out.shard.3": emptyFile(),
	}

	got, err := GlobAndCollectTestLopri(fsys)
	if err != nil {
		t.Fatalf("error collecting instances")
	}

	want := []TestLopriInstanceLogs{
		{
			Instance: "green",
			Clients: []TestLopriClientLogs{
				{
					Client: "node-1",
					Shards: []TestLopriClientShardLog{
						{Shard: 0, Path: "testlopri-green-client-node-1.out.shard.0"},
						{Shard: 1, Path: "testlopri-green-client-node-1.out.shard.1"},
						{Shard: 3, Path: "testlopri-green-client-node-1.out.shard.3"},
					},
				},
				{
					Client: "node-2",
					Shards: []TestLopriClientShardLog{
						{Shard: 0, Path: "testlopri-green-client-node-2.out"},
					},
				},
				{
					Client: "node-3",
					Shards: []TestLopriClientShardLog{
						{Shard: 0, Path: "testlopri-green-client-node-3.out"},
					},
				},
			},
		},
		{
			Instance: "purple",
			Clients: []TestLopriClientLogs{
				{
					Client: "node-1",
					Shards: []TestLopriClientShardLog{
						{Shard: 3, Path: "testlopri-purple-client-node-1.out.shard.3"},
					},
				},
			},
		},
		{
			Instance: "red",
			Clients: []TestLopriClientLogs{
				{
					Client: "node-5",
					Shards: []TestLopriClientShardLog{
						{Shard: 0, Path: "testlopri-red-client-node-5.out.shard.0"},
						{Shard: 1, Path: "testlopri-red-client-node-5.out.shard.1"},
					},
				},
				{
					Client: "node6",
					Shards: []TestLopriClientShardLog{
						{Shard: 1, Path: "testlopri-red-client-node6.out.shard.1"},
					},
				},
			},
		},
	}

	if !reflect.DeepEqual(got, want) {
		t.Errorf("got %+v\nwant %+v", got, want)
	}
}
