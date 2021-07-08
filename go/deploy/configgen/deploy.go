package configgen

import (
	"bytes"
	"crypto/rand"
	"encoding/xml"
	"fmt"
	"io"
	"os"

	"github.com/uluyol/heyp-agents/go/pb"
)

type rspec struct {
	XMLName xml.Name    `xml:"http://www.geni.net/resources/rspec/3 rspec"`
	Node    []rspecNode `xml:"node"`
}

type rspecNode struct {
	Interface    []rspecInterface `xml:"interface"`
	HardwareType struct {
		Name string `xml:"name,attr"`
	} `xml:"hardware_type"`
	Services struct {
		Login []rspecLogin `xml:"login"`
	} `xml:"services"`
}

type rspecInterface struct {
	IP []struct {
		Address string `xml:"address,attr"`
	} `xml:"ip"`
}

type rspecLogin struct {
	Authentication string `xml:"authentication,attr"`
	Hostname       string `xml:"hostname,attr"`
	Port           string `xml:"port,attr"`
	Username       string `xml:"username,attr"`
}

func randomBytes(buf []byte) error {
	_, err := rand.Read(buf)
	return err
}

func UpdateDeploymentConfig(c *pb.DeploymentConfig, cbytes []byte, manifestReader io.Reader, sshUser, outfile string, perm os.FileMode) error {
	var rspec rspec
	dec := xml.NewDecoder(manifestReader)
	if err := dec.Decode(&rspec); err != nil {
		return fmt.Errorf("failed to decode rspec: %w", err)
	}
	var curBuf, newBuf bytes.Buffer

	type rewrite struct {
		from, to []byte
	}

	var rewrites []rewrite

	// First record the rewrites that we want to do
MainLoop:
	for _, n := range c.GetNodes() {
		addrToFind := n.GetExperimentAddr()
		for _, n2 := range rspec.Node {
			thisNode := false
		IfaceLoop:
			for _, iface := range n2.Interface {
				for _, ip := range iface.IP {
					if ip.Address == addrToFind {
						thisNode = true
						break IfaceLoop
					}
				}
			}
			if thisNode {
				for _, login := range n2.Services.Login {
					if login.Username == sshUser {
						curBuf.Reset()
						newBuf.Reset()

						curBuf.WriteByte('"')
						curBuf.WriteString(n.GetExternalAddr())
						curBuf.WriteByte('"')

						newBuf.WriteByte('"')
						newBuf.WriteString(sshUser)
						newBuf.WriteByte('@')
						newBuf.WriteString(login.Hostname)
						newBuf.WriteByte('"')

						// Record that we want to do this rewrite
						rewrites = append(rewrites, rewrite{
							from: append([]byte(nil), curBuf.Bytes()...),
							to:   append([]byte(nil), newBuf.Bytes()...),
						})
						continue MainLoop
					}
				}
			}
		}
	}

	// Rewrite all instances of X to random tokens of equal length
	for i := range rewrites {
		rewrite := &rewrites[i]
		tok := make([]byte, len(rewrite.from))
		if err := randomBytes(tok); err != nil {
			return err
		}
		// fmt.Printf("replace %s with %s\n", rewrite.from, tok)
		for {
			idx := bytes.Index(cbytes, rewrite.from)
			if idx < 0 {
				break
			}
			copy(cbytes[idx:], tok)
		}
		rewrite.from = tok
	}

	// Replace the tokens with the desired bytes
	for _, rewrite := range rewrites {
		// fmt.Printf("replace %s with %s\n", rewrite.from, rewrite.to)
		cbytes = bytes.ReplaceAll(cbytes, rewrite.from, rewrite.to)
	}

	os.WriteFile(outfile, cbytes, perm)
	return nil
}
