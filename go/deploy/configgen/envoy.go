package configgen

import (
	"fmt"
	"html/template"
	"strings"
)

type EnvoyReverseProxy struct {
	Port      int
	AdminPort int
	Backends  []Backend
}

type Backend struct {
	Name               string
	LBPolicy           string // e.g. ROUND_ROBIN
	MaxConnections     int
	MaxPendingRequests int
	MaxRequests        int
	Remotes            []AddrAndPort
}

type AddrAndPort struct {
	Addr string
	Port int
}

var proxyConfigTmpl = template.Must(template.New("proxycfg").Parse(`
static_resources:
  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: {{.Port}}
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
{{- range .Backends }}
              - match:
                  prefix: "/service/{{.Name}}"
                route:
                  cluster: "{{.Name}}"
                  prefix_rewrite: "/"
{{- end }}
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
{{- range .Backends }}
  - name: "{{.Name}}"
    type: STATIC
    connect_timeout: 5s
    lb_policy: {{.LBPolicy}}
    load_assignment:
      cluster_name: "{{.Name}}"
      endpoints:
      - lb_endpoints:
{{- range .Remotes }}
        - endpoint:
            address:
              socket_address:
                address: {{.Addr}}
                port_value: {{.Port}}
{{- end}}
    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: {{.MaxConnections}}
        max_pending_requests: {{.MaxPendingRequests}}
        max_requests: {{.MaxRequests}}
      - priority: HIGH
        max_connections: {{.MaxConnections}}
        max_pending_requests: {{.MaxPendingRequests}}
        max_requests: {{.MaxRequests}}
{{- end }}

admin:
  address:
    socket_address:
      address: 0.0.0.0
      port_value: {{.AdminPort}}
layered_runtime:
  layers:
  - name: static_layer_0
    static_layer:
      envoy:
        resource_limits:
          listener:
            example_listener_name:
              connection_limit: 10000
`[1:]))

func (c *EnvoyReverseProxy) ToYAML() string {
	var buf strings.Builder
	err := proxyConfigTmpl.Execute(&buf, c)
	if err != nil {
		panic(fmt.Errorf("impossible error: %w", err))
	}
	return buf.String()
}
