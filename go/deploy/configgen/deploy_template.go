package configgen

import (
	"bytes"
	"encoding/xml"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strconv"

	"github.com/uluyol/heyp-agents/go/pb"
	starlarkmath "go.starlark.net/lib/math"
	starlarkproto "go.starlark.net/lib/proto"
	starlarktime "go.starlark.net/lib/time"
	"go.starlark.net/resolve"
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

func init() {
	resolve.AllowNestedDef = true
	resolve.AllowRecursion = true
	resolve.AllowSet = true
}

func GenDeploymentConfigs(filename string, configSetsToGen []string, externalAddrForIP []map[string]string) (configs map[string]*pb.DeploymentConfig, shardConfigs [][]string, err error) {
	// starExternalAddrForIP := starlark.NewDict(len(externalAddrForIP))
	// for k, v := range externalAddrForIP {
	// 	starExternalAddrForIP.SetKey(starlark.String(k), starlark.String(v))
	// }

	configSetsToGenList := starlark.NewList(nil)
	for _, s := range configSetsToGen {
		configSetsToGenList.Append(starlark.String(s))
	}

	nextShard := 0
	knownShards := make(map[string]int)
	starExternalAddrForIP := starlark.NewBuiltin("ext_addr_for_ip", func(thread *starlark.Thread, fn *starlark.Builtin, args starlark.Tuple, kwargs []starlark.Tuple) (starlark.Value, error) {
		var expIP, shardKey starlark.String
		err := starlark.UnpackPositionalArgs(fn.Name(), args, kwargs, 2, &expIP, &shardKey)
		if err != nil {
			return nil, err
		}
		shardIndex, known := knownShards[shardKey.GoString()]
		if !known {
			shardIndex = nextShard % len(externalAddrForIP)
			nextShard++
			knownShards[shardKey.GoString()] = shardIndex
		}
		addrMap := externalAddrForIP[shardIndex]
		return starlark.Tuple([]starlark.Value{starlark.String(addrMap[expIP.GoString()]), starlark.MakeInt(shardIndex)}), nil
	})

	starDivide := starlark.NewBuiltin("fdiv", func(thread *starlark.Thread, fn *starlark.Builtin, args starlark.Tuple, kwargs []starlark.Tuple) (starlark.Value, error) {
		var x, y starlark.Float
		err := starlark.UnpackPositionalArgs(fn.Name(), args, kwargs, 2, &x, &y)
		if err != nil {
			return nil, err
		}
		z := starlark.Float(float64(x) / float64(y))
		return z, nil
	})

	predeclared := starlark.StringDict{
		"math":            starlarkmath.Module,
		"proto":           starlarkproto.Module,
		"time":            starlarktime.Module,
		"ext_addr_for_ip": starExternalAddrForIP,
		"fdiv":            starDivide, // used because buildifier doesn't support floats
		"configs_to_gen":  configSetsToGenList,
	}

	// Execute the Starlark file.
	thread := &starlark.Thread{
		Print: func(_ *starlark.Thread, msg string) { log.Printf("gen-configs: %s", msg) },
	}

	starlarkproto.SetPool(thread, protoregistry.GlobalFiles)

	globals, err := starlark.ExecFile(thread, filename, nil, predeclared)
	if err != nil {
		if evalErr, ok := err.(*starlark.EvalError); ok {
			return nil, nil, fmt.Errorf("failed to generate configs: %v", evalErr.Backtrace())
		} else {
			return nil, nil, fmt.Errorf("failed to generate configs: %v", err)
		}
	}

	configs = make(map[string]*pb.DeploymentConfig)
	shardConfigs = make([][]string, len(externalAddrForIP))

	configsRaw := globals["configs"]
	if configsRaw == nil {
		return configs, shardConfigs, nil
	}

	configsMap, ok := configsRaw.(*starlark.Dict)
	if !ok {
		return nil, nil, fmt.Errorf("configs var needs to be of type dict, got %v", configsRaw)
	}

	for _, kv := range configsMap.Items() {
		name, ok := starlark.AsString(kv[0])
		if !ok {
			return nil, nil, fmt.Errorf("config key (name) must be string")
		}
		val := kv[1]
		if val == nil {
			return nil, nil, fmt.Errorf("got nil config for entry %s", name)
		}
		shardMesg, ok := val.(starlark.Tuple)
		if !ok {
			return nil, nil, fmt.Errorf("config value must be a (int, pb.DeploymentConfig) pair, got %v", val)
		}
		if shardMesg.Len() != 2 {
			return nil, nil, fmt.Errorf("config value must be a (int, pb.DeploymentConfig) pair, got %v", shardMesg)
		}
		shard, err := starlark.AsInt32(shardMesg.Index(0))
		if err != nil {
			return nil, nil, fmt.Errorf("config value must be a (int, pb.DeploymentConfig) pair, got %v", shardMesg)
		}
		shardConfigs[shard] = append(shardConfigs[shard], name)
		msg, ok := shardMesg.Index(1).(*starlarkproto.Message)
		if !ok {
			return nil, nil, fmt.Errorf("config type must be a DeploymentConfig, got %v", val)
		}
		configs[name], ok = msg.Message().(*pb.DeploymentConfig)
		if !ok {
			return nil, nil, fmt.Errorf("config type must be a DeploymentConfig, got %v", val)
		}
	}

	for _, cs := range shardConfigs {
		sort.Strings(cs)
	}

	return configs, shardConfigs, nil
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

func WriteShardConfigsTo(shardConfigs [][]string, outdir string) error {
	os.MkdirAll(outdir, 0o775)
	for shard, configs := range shardConfigs {
		var buf bytes.Buffer
		for _, c := range configs {
			buf.WriteString(c)
			buf.WriteString("\n")
		}

		p := filepath.Join(outdir, strconv.Itoa(shard))
		err := os.WriteFile(p, buf.Bytes(), 0o664)
		if err != nil {
			return fmt.Errorf("failed to write file %s: %v", p, err)
		}
	}
	return nil
}
