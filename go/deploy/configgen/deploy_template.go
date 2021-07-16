package configgen

import (
	"encoding/xml"
	"fmt"
	"log"
	"os"
	"path/filepath"

	"github.com/uluyol/heyp-agents/go/pb"
	starlarkproto "go.starlark.net/lib/proto"
	"go.starlark.net/starlark"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/reflect/protoregistry"
)

func ReadExternalAddrsMap(rspecPath, sshUser string) (map[string]string, error) {
	externalAddrForIP := make(map[string]string)
	if rspecPath == "" {
		return externalAddrForIP, nil
	}

	f, err := os.Open(rspecPath)
	if err != nil {
		return nil, fmt.Errorf("failed to open cloudlab rspec: %w", err)
	}
	defer f.Close()

	var rspec rspec
	dec := xml.NewDecoder(f)
	if err := dec.Decode(&rspec); err != nil {
		return nil, fmt.Errorf("failed to decode cloudlab rspec: %w", err)
	}

	for _, n := range rspec.Node {
		externalAddr := ""

		for _, login := range n.Services.Login {
			if login.Username == sshUser {
				externalAddr = sshUser + "@" + login.Hostname
				break
			}
		}

		for _, iface := range n.Interface {
			for _, ip := range iface.IP {
				externalAddrForIP[ip.Address] = externalAddr
			}
		}
	}

	return externalAddrForIP, nil
}

func GenDeploymentConfigs(filename string, externalAddrForIP map[string]string) (map[string]*pb.DeploymentConfig, error) {
	starExternalAddrForIP := starlark.NewDict(len(externalAddrForIP))
	for k, v := range externalAddrForIP {
		starExternalAddrForIP.SetKey(starlark.String(k), starlark.String(v))
	}

	predeclared := starlark.StringDict{
		"proto":           starlarkproto.Module,
		"ext_addr_for_ip": starExternalAddrForIP,
	}

	// Execute the Starlark file.
	thread := &starlark.Thread{
		Print: func(_ *starlark.Thread, msg string) { log.Printf("gen-configs: %s", msg) },
	}

	starlarkproto.SetPool(thread, protoregistry.GlobalFiles)

	globals, err := starlark.ExecFile(thread, filename, nil, predeclared)
	if err != nil {
		if evalErr, ok := err.(*starlark.EvalError); ok {
			return nil, fmt.Errorf("failed to generate configs: %v", evalErr.Backtrace())
		} else {
			return nil, fmt.Errorf("failed to generate configs: %v", err)
		}
	}

	configs := make(map[string]*pb.DeploymentConfig)

	configsRaw := globals["configs"]
	if configsRaw == nil {
		return configs, nil
	}

	configsMap, ok := configsRaw.(*starlark.Dict)
	if !ok {
		return nil, fmt.Errorf("configs var needs to be of type dict, got %v", configsRaw)
	}

	for _, kv := range configsMap.Items() {
		name, ok := starlark.AsString(kv[0])
		if !ok {
			return nil, fmt.Errorf("config key (name) must be string")
		}
		val := kv[1]
		if val == nil {
			return nil, fmt.Errorf("got nil config for entry %s", name)
		}
		msg, ok := val.(*starlarkproto.Message)
		if !ok {
			return nil, fmt.Errorf("config type must be a DeploymentConfig, got %v", val)
		}
		configs[name], ok = msg.Message().(*pb.DeploymentConfig)
		if !ok {
			return nil, fmt.Errorf("config type must be a DeploymentConfig, got %v", val)
		}
	}

	return configs, nil
}

func WriteConfigsTo(configs map[string]*pb.DeploymentConfig, outdir string) error {
	os.MkdirAll(outdir, 0o775)
	for name, c := range configs {
		b, err := prototext.MarshalOptions{
			Indent: "  ",
		}.Marshal(c)
		if err != nil {
			return fmt.Errorf("failed to marshal config for %s: %v", name, err)
		}

		p := filepath.Join(outdir, name+".textproto")
		os.WriteFile(p, b, 0o664)
		if err != nil {
			return fmt.Errorf("failed to write file %s: %v", p, err)
		}
	}
	return nil
}
