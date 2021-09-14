package actions

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"strconv"
	"text/template"
	"time"

	"github.com/uluyol/heyp-agents/go/multierrgroup"
	"github.com/uluyol/heyp-agents/go/pb"
)

type ipAddrRec struct {
	AddrInfo []struct {
		Local string `json:"local"`
	} `json:"addr_info"`
}

func iperfBW(b []byte) (int64, error) {
	fields := bytes.Split(bytes.Fields(b)[0], []byte{','})
	v, err := strconv.ParseInt(string(fields[len(fields)-1]), 10, 64)
	if err != nil {
		return 0, fmt.Errorf("failed to interpret iperf output: %v; output:\n%s", err, b)
	}
	return v, nil
}

func ReportPrioritizationBW(c *pb.DeploymentConfig) (hipri float64, lopri float64, err error) {
	if len(c.Nodes) < 3 {
		return 0, 0, errors.New("fewer than 3 nodes")
	}

	server := c.Nodes[0]
	c1 := c.Nodes[1]
	c2 := c.Nodes[2]

	cmd := TracingCommand(LogWithPrefix("report-pri-bw: "),
		"ssh", server.GetExternalAddr(), "iperf -s")
	if err := cmd.Start(); err != nil {
		return 0, 0, fmt.Errorf("failed to start iperf server: %w", err)
	}
	defer func() {
		cmd.Process.Kill()
		cmd.Wait()
	}()

	var outLO, outHI []byte

	var eg multierrgroup.Group
	eg.Go(func() error {
		cmd := TracingCommand(LogWithPrefix("report-pri-bw: "),
			"ssh", c1.GetExternalAddr(), "iperf -y C -S 0x00 -c "+server.GetExperimentAddr())
		var err error
		outLO, err = cmd.CombinedOutput()
		if err != nil {
			return fmt.Errorf("failure running LOPRI client: %w", err)
		}
		return nil
	})
	eg.Go(func() error {
		cmd := TracingCommand(LogWithPrefix("report-pri-bw: "),
			"ssh", c2.GetExternalAddr(), "iperf -y C -S 0x48 -c "+server.GetExperimentAddr())
		var err error
		outHI, err = cmd.CombinedOutput()
		if err != nil {
			return fmt.Errorf("failure running HIPRI client: %w", err)
		}
		return nil
	})

	if err := eg.Wait(); err != nil {
		return 0, 0, err
	}

	loBW, err := iperfBW(outLO)
	if err != nil {
		return 0, 0, err
	}
	hiBW, err := iperfBW(outHI)
	if err != nil {
		return 0, 0, err
	}

	loGbps := float64(loBW) / (1 << 30)
	hiGbps := float64(hiBW) / (1 << 30)

	return hiGbps, loGbps, nil
}

func CheckNodeIPs(c *pb.DeploymentConfig) error {
	var eg multierrgroup.Group

	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("check-node-ips: "),
				"ssh", n.GetExternalAddr(),
				"ip -json addr",
			)
			rc, err := cmd.StdoutPipe("ip_addr.json")

			if err != nil {
				return fmt.Errorf("failed to create stdout pipe for Node %q: %w", n.GetName(), err)
			}

			if err := cmd.Start(); err != nil {
				return fmt.Errorf("failed to contact Node %q: %w", n.GetName(), err)
			}

			dec := json.NewDecoder(rc)
			var recs []ipAddrRec
			decErr := dec.Decode(&recs)
			io.Copy(io.Discard, rc)
			rc.Close()
			cmd.Wait()

			if decErr != nil {
				return fmt.Errorf("failed to decode ip -json addr output on Node %q: %w", n.GetName(), decErr)
			}

			found := false
			for _, r := range recs {
				for _, ai := range r.AddrInfo {
					if ai.Local == n.GetExperimentAddr() {
						found = true
					}
				}
			}
			if !found {
				return fmt.Errorf("ip address %s not found on Node %q: got %+v from ip addr", n.GetExperimentAddr(), n.GetName(), recs)
			}
			return nil
		})
	}

	return eg.Wait()
}

var curlRunTmpl = template.Must(template.New("curl-run").Parse(`
ret=0
src={{ .Src }}

{{ range .Dests }}
out=$(curl -s --retry 5 --retry-delay 2 http://{{.}}:8000/ 2>&1)
if [[ $? -ne 0 ]]; then
	echo fail $src to {{.}}: output >&2
	echo "$out"
	ret=1
fi
{{ end }}

exit $ret
`))

func CheckNodeConnectivity(c *pb.DeploymentConfig) error {
	var startEg multierrgroup.Group

	cmds := make([]*TracingCmd, len(c.Nodes))

	for i, n := range c.Nodes {
		i := i
		n := n
		startEg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("check-node-connectivity: "),
				"ssh", n.GetExternalAddr(),
				"python3 -m http.server",
			)

			cmds[i] = cmd

			return cmd.Start()
		})
	}

	defer func() {
		for _, c := range cmds {
			if c != nil {
				c.Process.Kill()
			}
		}
	}()

	if err := startEg.Wait(); err != nil {
		return err
	}

	var eg multierrgroup.Group

	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			var tmplData struct {
				Src   string
				Dests []string
			}
			tmplData.Src = n.GetExperimentAddr()
			for _, n := range c.Nodes {
				tmplData.Dests = append(tmplData.Dests, n.GetExperimentAddr())
			}

			var buf bytes.Buffer
			if err := curlRunTmpl.Execute(&buf, &tmplData); err != nil {
				return err
			}

			cmd := TracingCommand(
				LogWithPrefix("check-node-connectivity: "),
				"ssh", n.GetExternalAddr(), "bash",
			)
			cmd.SetStdin("curl-runs.bash", &buf)
			out, err := cmd.CombinedOutput()

			if err != nil {
				return fmt.Errorf("src %s failed to connect to all: %v; output:\n%s", n.GetName(), err, out)
			}

			return nil
		})
	}

	if err := eg.Wait(); err != nil {
		return err
	}

	return nil
}

func KillIPerf(c *pb.DeploymentConfig) error {
	var eg multierrgroup.Group

	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("measure-node-bw: "),
				"ssh", n.GetExternalAddr(),
				"killall iperf || true",
			)

			if out, err := cmd.CombinedOutput(); err != nil {
				return fmt.Errorf("%s: faild to kill iperf: %v; output:\n%s", n.GetName(), err, out)
			}

			return nil
		})
	}

	return eg.Wait()
}

func MeasureNodeBandwidth(c *pb.DeploymentConfig) error {
	var startEg multierrgroup.Group

	for _, n := range c.Nodes {
		n := n
		startEg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("measure-node-bw: "),
				"ssh", n.GetExternalAddr(),
				"killall iperf; iperf -s -D",
			)

			if out, err := cmd.CombinedOutput(); err != nil {
				return fmt.Errorf("%s: faild to start iperf server: %v; output:\n%s", n.GetName(), err, out)
			}

			return nil
		})
	}

	if err := startEg.Wait(); err != nil {
		return err
	}

	rounds := allToAllWithoutSharing(len(c.Nodes))

	bwBps := make([][]int64, len(c.Nodes))
	{
		x := make([]int64, len(c.Nodes)*len(c.Nodes))
		for i := 0; i < len(c.Nodes); i++ {
			bwBps[i] = x[i*len(c.Nodes) : (i+1)*len(c.Nodes)]
		}
	}

	log.Print("waiting for servers to start")
	time.Sleep(3 * time.Second)

	for ri, round := range rounds {
		log.Printf("round %d/%d", ri, len(rounds))

		var eg multierrgroup.Group

		for _, p := range round {
			p := p

			dst := c.Nodes[p.dst]
			src := c.Nodes[p.src]
			eg.Go(func() error {
				cmd := TracingCommand(
					LogWithPrefix("measure-node-bw: "),
					"ssh", src.GetExternalAddr(), "iperf", "-t", "4", "-c", dst.GetExperimentAddr(), "-y", "C")
				var bout, berr bytes.Buffer
				cmd.SetStdout("out", &bout)
				cmd.SetStderr("err", &berr)
				err := cmd.Run()

				if err != nil || bytes.Contains(berr.Bytes(), []byte("failed")) {
					return fmt.Errorf("src %s failed to run iperf against %s: %v; output:\n%s\nerr:\n%s", src.GetName(), dst.GetName(), err, bout.Bytes(), berr.Bytes())
				}

				fields := bytes.Split(bytes.Fields(bout.Bytes())[0], []byte{','})
				v, err := strconv.ParseInt(string(fields[len(fields)-1]), 10, 64)
				if err != nil {
					return fmt.Errorf("src %s failed to interpret iperf output against %s: %v; output:\n%s\nerr:\n%s", src.GetName(), dst.GetName(), err, bout.Bytes(), berr.Bytes())
				}
				bwBps[p.src][p.dst] = v

				return nil
			})
		}

		if err := eg.Wait(); err != nil {
			return err
		}
	}

	fmt.Printf("SRC\tDST\tBW-GBPS\n")
	for i := 0; i < len(c.Nodes); i++ {
		for j := 0; j < len(c.Nodes); j++ {
			if i == j {
				continue
			}

			gbps := float64(bwBps[i][j]) / (1 << 30)
			fmt.Printf("%s\t%s\t%f\n", c.Nodes[i].GetName(), c.Nodes[j].GetName(), gbps)
		}
	}

	return nil
}
