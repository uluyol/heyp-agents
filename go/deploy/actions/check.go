package actions

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"text/template"

	"github.com/uluyol/heyp-agents/go/multierrgroup"
	"github.com/uluyol/heyp-agents/go/pb"
)

type ipAddrRec struct {
	AddrInfo []struct {
		Local string `json:"local"`
	} `json:"addr_info"`
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
