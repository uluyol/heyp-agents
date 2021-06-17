package configgen

import (
	"io/ioutil"
	"os"
	"os/exec"
	"testing"
)

func TestEnvoyReverseProxy(t *testing.T) {
	cfg1 := EnvoyReverseProxy{
		Port:      111,
		AdminPort: 112,
		Backends: []Backend{
			{
				LBPolicy: "ROUND_ROBIN",
				Remotes: []AddrAndPort{
					{"1.1.1.1", 7777},
					{"2.2.2.2", 12},
				},
			},
			{
				LBPolicy: "LEAST_REQUEST",
				Remotes: []AddrAndPort{
					{"127.0.0.1", 123},
					{"10.0.0.2", 88},
				},
			},
		},
	}

	const want1 = `static_resources:
  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 111
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          codec_type: AUTO
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: backend
              domains:
              - "*"
              routes:
              - match:
                  prefix: "/service/0"
                route:
                  cluster: service0
                  prefix_rewrite: "/"
              - match:
                  prefix: "/service/1"
                route:
                  cluster: service1
                  prefix_rewrite: "/"
          http_filters:
          - name: envoy.filters.http.router
            typed_config: {}

  clusters:
  - name: service0
    type: STATIC
    connect_timeout: 5s
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: service0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 1.1.1.1
                port_value: 7777
        - endpoint:
            address:
              socket_address:
                address: 2.2.2.2
                port_value: 12
  - name: service1
    type: STATIC
    connect_timeout: 5s
    lb_policy: LEAST_REQUEST
    load_assignment:
      cluster_name: service1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 123
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.2
                port_value: 88

admin:
  address:
    socket_address:
      address: 0.0.0.0
      port_value: 112
layered_runtime:
  layers:
  - name: static_layer_0
    static_layer:
      envoy:
        resource_limits:
          listener:
            example_listener_name:
              connection_limit: 10000
`

	cfg2 := EnvoyReverseProxy{
		Port:      99,
		AdminPort: 4,
		Backends: []Backend{
			{
				LBPolicy: "ROUND_ROBIN",
				Remotes: []AddrAndPort{
					{"1.1.1.1", 7777},
					{"2.2.2.2", 12},
				},
			},
			{
				LBPolicy: "LEAST_REQUEST",
				Remotes: []AddrAndPort{
					{"127.0.0.1", 123},
					{"10.0.0.2", 88},
				},
			},
		},
	}

	const want2 = `static_resources:
  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 99
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          codec_type: AUTO
          stat_prefix: ingress_http
          route_config:
            name: local_route
            virtual_hosts:
            - name: backend
              domains:
              - "*"
              routes:
              - match:
                  prefix: "/service/0"
                route:
                  cluster: service0
                  prefix_rewrite: "/"
              - match:
                  prefix: "/service/1"
                route:
                  cluster: service1
                  prefix_rewrite: "/"
          http_filters:
          - name: envoy.filters.http.router
            typed_config: {}

  clusters:
  - name: service0
    type: STATIC
    connect_timeout: 5s
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: service0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 1.1.1.1
                port_value: 7777
        - endpoint:
            address:
              socket_address:
                address: 2.2.2.2
                port_value: 12
  - name: service1
    type: STATIC
    connect_timeout: 5s
    lb_policy: LEAST_REQUEST
    load_assignment:
      cluster_name: service1
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 123
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.2
                port_value: 88

admin:
  address:
    socket_address:
      address: 0.0.0.0
      port_value: 4
layered_runtime:
  layers:
  - name: static_layer_0
    static_layer:
      envoy:
        resource_limits:
          listener:
            example_listener_name:
              connection_limit: 10000
`

	fatalMismatch := func(name, have, want string) {
		t.Helper()
		d, err := diff(want, have)
		if err != nil {
			t.Fatalf("%s mismatch: have:\n%s\nwant:\n%s", name, have, want)
		} else {
			t.Fatalf("%s mismatch: diff:\n%s", name, d)
		}
	}

	if have := cfg1.ToYAML(); have != want1 {
		fatalMismatch("cfg1", have, want1)
	}

	if have := cfg2.ToYAML(); have != want2 {
		fatalMismatch("cfg2", have, want2)
	}
}

func diff(s1, s2 string) ([]byte, error) {
	p1, err := writeTempFile("todiff", s1)
	if err != nil {
		return nil, err
	}
	defer os.Remove(p1)

	p2, err := writeTempFile("todiff", s2)
	if err != nil {
		return nil, err
	}
	defer os.Remove(p2)

	out, err := exec.Command("diff", "-u", p1, p2).CombinedOutput()
	if len(out) > 0 {
		err = nil
	}
	return out, err
}

func writeTempFile(prefix string, data string) (string, error) {
	file, err := ioutil.TempFile("", prefix)
	if err != nil {
		return "", err
	}
	_, err = file.WriteString(data)
	if err1 := file.Close(); err == nil {
		err = err1
	}
	if err != nil {
		os.Remove(file.Name())
		return "", err
	}
	return file.Name(), nil
}
