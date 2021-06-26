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
				Name:               "AB",
				LBPolicy:           "ROUND_ROBIN",
				MaxConnections:     2,
				MaxPendingRequests: 1234,
				MaxRequests:        421,
				Remotes: []AddrAndPort{
					{"1.1.1.1", 7777},
					{"2.2.2.2", 12},
				},
			},
			{
				Name:     "backend-2",
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
                  prefix: "/service/AB"
                route:
                  cluster: "AB"
                  prefix_rewrite: "/"
              - match:
                  prefix: "/service/backend-2"
                route:
                  cluster: "backend-2"
                  prefix_rewrite: "/"
          http_filters:
          - name: "envoy.filters.http.lua"
            typed_config:
              "@type": "type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua"
              inline_code: |
                function envoy_on_response(response_handle)
                  if response_handle:headers():get(":status") == "200" then
                    -- Sets the content-length.
                    response_handle:headers():add("exp-orig-content-length", response_handle:headers():get("content-length"))
                    response_handle:headers():replace("content-length", 0)
                    response_handle:headers():replace("content-type", "text/plain")

                    -- Truncate data
                    local last
                    for chunk in response_handle:bodyChunks() do
                      -- Clears each received chunk.
                      chunk:setBytes("")
                      last = chunk
                    end

                    last:setBytes("")
                  end
                end
          - name: envoy.filters.http.router
            typed_config: {}

  clusters:
  - name: "AB"
    type: STATIC
    connect_timeout: 5s
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: "AB"
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
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 2
        max_pending_requests: 1234
        max_requests: 421
      - priority: HIGH
        max_connections: 2
        max_pending_requests: 1234
        max_requests: 421
  - name: "backend-2"
    type: STATIC
    connect_timeout: 5s
    lb_policy: LEAST_REQUEST
    load_assignment:
      cluster_name: "backend-2"
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
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 0
        max_pending_requests: 0
        max_requests: 0
      - priority: HIGH
        max_connections: 0
        max_pending_requests: 0
        max_requests: 0

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
				Name:     "1",
				LBPolicy: "ROUND_ROBIN",
				Remotes: []AddrAndPort{
					{"1.1.1.1", 7777},
					{"2.2.2.2", 12},
				},
			},
			{
				Name:               "ZzZ",
				LBPolicy:           "LEAST_REQUEST",
				MaxConnections:     2000,
				MaxPendingRequests: 134,
				MaxRequests:        42,
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
                  prefix: "/service/1"
                route:
                  cluster: "1"
                  prefix_rewrite: "/"
              - match:
                  prefix: "/service/ZzZ"
                route:
                  cluster: "ZzZ"
                  prefix_rewrite: "/"
          http_filters:
          - name: "envoy.filters.http.lua"
            typed_config:
              "@type": "type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua"
              inline_code: |
                function envoy_on_response(response_handle)
                  if response_handle:headers():get(":status") == "200" then
                    -- Sets the content-length.
                    response_handle:headers():add("exp-orig-content-length", response_handle:headers():get("content-length"))
                    response_handle:headers():replace("content-length", 0)
                    response_handle:headers():replace("content-type", "text/plain")

                    -- Truncate data
                    local last
                    for chunk in response_handle:bodyChunks() do
                      -- Clears each received chunk.
                      chunk:setBytes("")
                      last = chunk
                    end

                    last:setBytes("")
                  end
                end
          - name: envoy.filters.http.router
            typed_config: {}

  clusters:
  - name: "1"
    type: STATIC
    connect_timeout: 5s
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: "1"
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
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 0
        max_pending_requests: 0
        max_requests: 0
      - priority: HIGH
        max_connections: 0
        max_pending_requests: 0
        max_requests: 0
  - name: "ZzZ"
    type: STATIC
    connect_timeout: 5s
    lb_policy: LEAST_REQUEST
    load_assignment:
      cluster_name: "ZzZ"
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
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 2000
        max_pending_requests: 134
        max_requests: 42
      - priority: HIGH
        max_connections: 2000
        max_pending_requests: 134
        max_requests: 42

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
