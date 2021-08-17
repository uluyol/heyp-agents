package configgen

import (
	"fmt"
	"html/template"
	"strings"
)

type EnvoyReverseProxy struct {
	AdminPort int
	Listeners []EnvoyListener
}

type EnvoyListener struct {
	Port             int
	AdmissionControl EnvoyAdmissionControl
	Backends         []Backend
}

type EnvoyAdmissionControl struct {
	Enabled           bool
	SamplingWindowSec float64
	SuccessRateThresh float64
	Aggression        float64
	RPSThresh         float64
	MaxRejectionProb  float64
}

type Backend struct {
	Name               string
	LBPolicy           string // e.g. ROUND_ROBIN
	TimeoutSec         float64
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
{{- range .Listeners }}
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
{{- if ne .TimeoutSec 0.0 }}
                  timeout: {{.TimeoutSec}}s
{{- end }}
{{- end }}
          http_filters:
{{- if .AdmissionControl.Enabled }}
          - name: envoy.filters.http.admission_control
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.admission_control.v3alpha.AdmissionControl
              enabled:
                default_value: true
                runtime_key: "admission_control.enabled"
              sampling_window: {{.AdmissionControl.SamplingWindowSec}}s
              sr_threshold:
                default_value:
                  value: {{.AdmissionControl.SuccessRateThresh}}
                runtime_key: "admission_control.sr_threshold"
              aggression:
                default_value: {{.AdmissionControl.Aggression}}
                runtime_key: "admission_control.aggression"
              rps_threshold:
                default_value: {{.AdmissionControl.RPSThresh}}
                runtime_key: "admission_control.rps_threshold"
              max_rejection_probability:
                default_value:
                  value: {{.AdmissionControl.MaxRejectionProb}}
                runtime_key: "admission_control.max_rejection_probability"
              success_criteria:
                http_criteria:
                  http_success_status:
                    - start: 100
                      end:   400
                    - start: 404
                      end:   404
{{- end }}
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
{{- end }}
  clusters:
{{- range .Listeners }}
{{- range .Backends }}
  - name: "{{.Name}}"
    type: STATIC
    connect_timeout: 5s
    lb_policy: {{.LBPolicy}}
    http2_protocol_options:
      max_concurrent_streams: 20
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
{{- end }}

admin:
  address:
    socket_address:
      address: 0.0.0.0
      port_value: {{.AdminPort}}
`[1:]))

func (c *EnvoyReverseProxy) ToYAML() string {
	var buf strings.Builder
	err := proxyConfigTmpl.Execute(&buf, c)
	if err != nil {
		panic(fmt.Errorf("impossible error: %w", err))
	}
	return buf.String()
}
