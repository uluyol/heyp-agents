package types

type Machine struct {
	User string `json:"user"`
	Addr string `json:"addr"`
}

type ClusterAgent struct {
	M Machine `json:"m"`
}

type HostAgent struct {
	M Machine `json:"m"`
}

type TestLOPRIServer struct{}

type TestLOPRIClient struct{}

type Config struct {
	ClusterAgent     ClusterAgent      `json:"clusterAgent"`
	HostAgents       []HostAgent       `json:"hostAgents"`
	TestLOPRIServers []TestLOPRIServer `json:"testLOPRIServers"`
	TestLOPRIClients []TestLOPRIClient `json:"testLOPRIClient"`
}
