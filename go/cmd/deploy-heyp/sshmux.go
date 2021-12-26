package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/sshmux"
	"golang.org/x/net/context"
)

var mkSSHMuxCmd = &configCmd{
	name:     "mk-ssh-mux",
	synopsis: "create an an ssh mux for a deployment, add the printed dir to your PATH",
	exec: func(c *pb.DeploymentConfig) error {
		tmpdir, err := os.MkdirTemp("", "heyp.sshmux.*")
		if err != nil {
			return fmt.Errorf("failed to create mux dir: %w", err)
		}

		mux := sshmux.NewMux(tmpdir)
		var wg sync.WaitGroup
		for _, node := range c.Nodes {
			wg.Add(1)
			go func(node *pb.DeployedNode) {
				defer wg.Done()
				var err error
				const maxTries = 5
				for try := 0; try < maxTries; try++ {
					err = mux.CreateMaster(node.GetExternalAddr())
					if err == nil {
						break
					}
					time.Sleep(23 * time.Millisecond)
				}
				if err != nil {
					log.Printf("failed to create mux for %s: %v", node.GetExternalAddr(), err)
				}
			}(node)
		}
		wg.Wait()

		f, err := os.Create(filepath.Join(tmpdir, "mux.json"))
		if err != nil {
			return fmt.Errorf("failed to create mux state file: %w", err)
		}
		enc := json.NewEncoder(f)
		if err := enc.Encode(mux); err != nil {
			return fmt.Errorf("failed to write mux state file: %w", err)
		}
		if err := f.Close(); err != nil {
			return fmt.Errorf("failed to close mux state file: %w", err)
		}

		if err := mux.GenSSHWraper(filepath.Join(tmpdir, "ssh")); err != nil {
			return fmt.Errorf("failed to create ssh wrapper: %w", err)
		}
		fmt.Println(tmpdir)
		return nil
	},
}

type delSSHMuxCmd struct{ muxPath string }

func (*delSSHMuxCmd) Name() string     { return "del-ssh-mux" }
func (*delSSHMuxCmd) Synopsis() string { return "delete an an ssh mux" }
func (*delSSHMuxCmd) Usage() string    { return "" }

func (c *delSSHMuxCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.muxPath, "mux", "", "path to mux state directory")
}

func (c *delSSHMuxCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	if c.muxPath == "" {
		// no mux
		return subcommands.ExitSuccess
	}

	mux := new(sshmux.Mux)
	data, err := os.ReadFile(filepath.Join(c.muxPath, "mux.json"))
	if err != nil {
		log.Fatalf("failed to read mux state: %v", err)
	}
	if err := json.Unmarshal(data, mux); err != nil {
		log.Fatalf("failed to decode mux state: %v", err)
	}
	if err := mux.ReleaseAll(); err != nil {
		log.Printf("failed to release all master conns: %v", err)
	}
	if err := os.RemoveAll(c.muxPath); err != nil {
		log.Printf("failed to delete all mux state: %v", err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(delSSHMuxCmd)
