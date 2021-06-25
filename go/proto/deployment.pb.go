// This file contains definitions of all configuration protos.
// These messages should only be stored in textual or in-memory form (i.e. not
// the binary format), so feed free to change any field numbers.

// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.25.0-devel
// 	protoc        v3.12.4
// source: heyp/proto/deployment.proto

package proto

import (
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type DeployedNode struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Name           *string `protobuf:"bytes,1,opt,name=name" json:"name,omitempty"`
	ExternalAddr   *string `protobuf:"bytes,2,opt,name=external_addr,json=externalAddr" json:"external_addr,omitempty"`
	ExperimentAddr *string `protobuf:"bytes,3,opt,name=experiment_addr,json=experimentAddr" json:"experiment_addr,omitempty"`
	// Valid roles:
	// - host-agent
	// - cluster-agent
	// - testlopri-[name]-server
	// - testlopri-[name]-client
	// - fortio-[group]-envoy-proxy
	// - fortio-[group]-[name]-server
	// - fortio-[group]-[name]-client
	Roles []string `protobuf:"bytes,10,rep,name=roles" json:"roles,omitempty"`
}

func (x *DeployedNode) Reset() {
	*x = DeployedNode{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_deployment_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *DeployedNode) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*DeployedNode) ProtoMessage() {}

func (x *DeployedNode) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_deployment_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use DeployedNode.ProtoReflect.Descriptor instead.
func (*DeployedNode) Descriptor() ([]byte, []int) {
	return file_heyp_proto_deployment_proto_rawDescGZIP(), []int{0}
}

func (x *DeployedNode) GetName() string {
	if x != nil && x.Name != nil {
		return *x.Name
	}
	return ""
}

func (x *DeployedNode) GetExternalAddr() string {
	if x != nil && x.ExternalAddr != nil {
		return *x.ExternalAddr
	}
	return ""
}

func (x *DeployedNode) GetExperimentAddr() string {
	if x != nil && x.ExperimentAddr != nil {
		return *x.ExperimentAddr
	}
	return ""
}

func (x *DeployedNode) GetRoles() []string {
	if x != nil {
		return x.Roles
	}
	return nil
}

type DeployedCluster struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Name             *string      `protobuf:"bytes,1,opt,name=name" json:"name,omitempty"`
	NodeNames        []string     `protobuf:"bytes,2,rep,name=node_names,json=nodeNames" json:"node_names,omitempty"`
	Limits           *AllocBundle `protobuf:"bytes,3,opt,name=limits" json:"limits,omitempty"`
	ClusterAgentPort *int32       `protobuf:"varint,4,opt,name=cluster_agent_port,json=clusterAgentPort" json:"cluster_agent_port,omitempty"`
}

func (x *DeployedCluster) Reset() {
	*x = DeployedCluster{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_deployment_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *DeployedCluster) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*DeployedCluster) ProtoMessage() {}

func (x *DeployedCluster) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_deployment_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use DeployedCluster.ProtoReflect.Descriptor instead.
func (*DeployedCluster) Descriptor() ([]byte, []int) {
	return file_heyp_proto_deployment_proto_rawDescGZIP(), []int{1}
}

func (x *DeployedCluster) GetName() string {
	if x != nil && x.Name != nil {
		return *x.Name
	}
	return ""
}

func (x *DeployedCluster) GetNodeNames() []string {
	if x != nil {
		return x.NodeNames
	}
	return nil
}

func (x *DeployedCluster) GetLimits() *AllocBundle {
	if x != nil {
		return x.Limits
	}
	return nil
}

func (x *DeployedCluster) GetClusterAgentPort() int32 {
	if x != nil && x.ClusterAgentPort != nil {
		return *x.ClusterAgentPort
	}
	return 0
}

type DeployedTestLopriInstance struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Node roles:
	// testlopri-[name]-client
	// testlopri-[name]-server
	Name            *string                `protobuf:"bytes,1,opt,name=name" json:"name,omitempty"`
	ServePort       *int32                 `protobuf:"varint,2,opt,name=serve_port,json=servePort" json:"serve_port,omitempty"`
	Client          *TestLopriClientConfig `protobuf:"bytes,3,opt,name=client" json:"client,omitempty"`
	NumClientShards *int32                 `protobuf:"varint,4,opt,name=num_client_shards,json=numClientShards,def=1" json:"num_client_shards,omitempty"`
	NumServerShards *int32                 `protobuf:"varint,5,opt,name=num_server_shards,json=numServerShards,def=1" json:"num_server_shards,omitempty"`
}

// Default values for DeployedTestLopriInstance fields.
const (
	Default_DeployedTestLopriInstance_NumClientShards = int32(1)
	Default_DeployedTestLopriInstance_NumServerShards = int32(1)
)

func (x *DeployedTestLopriInstance) Reset() {
	*x = DeployedTestLopriInstance{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_deployment_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *DeployedTestLopriInstance) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*DeployedTestLopriInstance) ProtoMessage() {}

func (x *DeployedTestLopriInstance) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_deployment_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use DeployedTestLopriInstance.ProtoReflect.Descriptor instead.
func (*DeployedTestLopriInstance) Descriptor() ([]byte, []int) {
	return file_heyp_proto_deployment_proto_rawDescGZIP(), []int{2}
}

func (x *DeployedTestLopriInstance) GetName() string {
	if x != nil && x.Name != nil {
		return *x.Name
	}
	return ""
}

func (x *DeployedTestLopriInstance) GetServePort() int32 {
	if x != nil && x.ServePort != nil {
		return *x.ServePort
	}
	return 0
}

func (x *DeployedTestLopriInstance) GetClient() *TestLopriClientConfig {
	if x != nil {
		return x.Client
	}
	return nil
}

func (x *DeployedTestLopriInstance) GetNumClientShards() int32 {
	if x != nil && x.NumClientShards != nil {
		return *x.NumClientShards
	}
	return Default_DeployedTestLopriInstance_NumClientShards
}

func (x *DeployedTestLopriInstance) GetNumServerShards() int32 {
	if x != nil && x.NumServerShards != nil {
		return *x.NumServerShards
	}
	return Default_DeployedTestLopriInstance_NumServerShards
}

type DeployedFortioInstance struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	// Node roles:
	// fortio-[group]-[name]-server
	// fortio-[group]-[name]-client
	// fortio-[group]-envoy-proxy
	Group     *string             `protobuf:"bytes,1,opt,name=group" json:"group,omitempty"`
	Name      *string             `protobuf:"bytes,2,opt,name=name" json:"name,omitempty"`
	ServePort *int32              `protobuf:"varint,3,opt,name=serve_port,json=servePort,def=9911" json:"serve_port,omitempty"`
	LbPolicy  *string             `protobuf:"bytes,4,opt,name=lb_policy,json=lbPolicy,def=ROUND_ROBIN" json:"lb_policy,omitempty"`
	Client    *FortioClientConfig `protobuf:"bytes,5,opt,name=client" json:"client,omitempty"`
}

// Default values for DeployedFortioInstance fields.
const (
	Default_DeployedFortioInstance_ServePort = int32(9911)
	Default_DeployedFortioInstance_LbPolicy  = string("ROUND_ROBIN")
)

func (x *DeployedFortioInstance) Reset() {
	*x = DeployedFortioInstance{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_deployment_proto_msgTypes[3]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *DeployedFortioInstance) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*DeployedFortioInstance) ProtoMessage() {}

func (x *DeployedFortioInstance) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_deployment_proto_msgTypes[3]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use DeployedFortioInstance.ProtoReflect.Descriptor instead.
func (*DeployedFortioInstance) Descriptor() ([]byte, []int) {
	return file_heyp_proto_deployment_proto_rawDescGZIP(), []int{3}
}

func (x *DeployedFortioInstance) GetGroup() string {
	if x != nil && x.Group != nil {
		return *x.Group
	}
	return ""
}

func (x *DeployedFortioInstance) GetName() string {
	if x != nil && x.Name != nil {
		return *x.Name
	}
	return ""
}

func (x *DeployedFortioInstance) GetServePort() int32 {
	if x != nil && x.ServePort != nil {
		return *x.ServePort
	}
	return Default_DeployedFortioInstance_ServePort
}

func (x *DeployedFortioInstance) GetLbPolicy() string {
	if x != nil && x.LbPolicy != nil {
		return *x.LbPolicy
	}
	return Default_DeployedFortioInstance_LbPolicy
}

func (x *DeployedFortioInstance) GetClient() *FortioClientConfig {
	if x != nil {
		return x.Client
	}
	return nil
}

type DeployedFortioGroup struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Name           *string `protobuf:"bytes,1,opt,name=name" json:"name,omitempty"`
	EnvoyPort      *int32  `protobuf:"varint,2,opt,name=envoy_port,json=envoyPort" json:"envoy_port,omitempty"`
	EnvoyAdminPort *int32  `protobuf:"varint,3,opt,name=envoy_admin_port,json=envoyAdminPort" json:"envoy_admin_port,omitempty"`
}

func (x *DeployedFortioGroup) Reset() {
	*x = DeployedFortioGroup{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_deployment_proto_msgTypes[4]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *DeployedFortioGroup) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*DeployedFortioGroup) ProtoMessage() {}

func (x *DeployedFortioGroup) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_deployment_proto_msgTypes[4]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use DeployedFortioGroup.ProtoReflect.Descriptor instead.
func (*DeployedFortioGroup) Descriptor() ([]byte, []int) {
	return file_heyp_proto_deployment_proto_rawDescGZIP(), []int{4}
}

func (x *DeployedFortioGroup) GetName() string {
	if x != nil && x.Name != nil {
		return *x.Name
	}
	return ""
}

func (x *DeployedFortioGroup) GetEnvoyPort() int32 {
	if x != nil && x.EnvoyPort != nil {
		return *x.EnvoyPort
	}
	return 0
}

func (x *DeployedFortioGroup) GetEnvoyAdminPort() int32 {
	if x != nil && x.EnvoyAdminPort != nil {
		return *x.EnvoyAdminPort
	}
	return 0
}

type DeploymentConfig struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Nodes    []*DeployedNode    `protobuf:"bytes,1,rep,name=nodes" json:"nodes,omitempty"`
	Clusters []*DeployedCluster `protobuf:"bytes,2,rep,name=clusters" json:"clusters,omitempty"`
	// Fields that are autofilled:
	// - cluster_agent_config.server.address (using clusters.cluster_agent_port)
	ClusterAgentConfig *ClusterAgentConfig `protobuf:"bytes,10,opt,name=cluster_agent_config,json=clusterAgentConfig" json:"cluster_agent_config,omitempty"`
	// Fields that are autofilled:
	// - host_agent_template.flow_state_reporter.this_host_addrs
	// - host_agent_template.daemon.cluster_agent_addr
	// - host_agent_template.dc_mapper
	HostAgentTemplate *HostAgentConfig `protobuf:"bytes,11,opt,name=host_agent_template,json=hostAgentTemplate" json:"host_agent_template,omitempty"`
	// Workloads to run (all are run in parallel).
	TestlopriInstances []*DeployedTestLopriInstance `protobuf:"bytes,12,rep,name=testlopri_instances,json=testlopriInstances" json:"testlopri_instances,omitempty"`
	FortioGroups       []*DeployedFortioGroup       `protobuf:"bytes,13,rep,name=fortio_groups,json=fortioGroups" json:"fortio_groups,omitempty"`
	FortioInstances    []*DeployedFortioInstance    `protobuf:"bytes,14,rep,name=fortio_instances,json=fortioInstances" json:"fortio_instances,omitempty"`
}

func (x *DeploymentConfig) Reset() {
	*x = DeploymentConfig{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_deployment_proto_msgTypes[5]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *DeploymentConfig) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*DeploymentConfig) ProtoMessage() {}

func (x *DeploymentConfig) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_deployment_proto_msgTypes[5]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use DeploymentConfig.ProtoReflect.Descriptor instead.
func (*DeploymentConfig) Descriptor() ([]byte, []int) {
	return file_heyp_proto_deployment_proto_rawDescGZIP(), []int{5}
}

func (x *DeploymentConfig) GetNodes() []*DeployedNode {
	if x != nil {
		return x.Nodes
	}
	return nil
}

func (x *DeploymentConfig) GetClusters() []*DeployedCluster {
	if x != nil {
		return x.Clusters
	}
	return nil
}

func (x *DeploymentConfig) GetClusterAgentConfig() *ClusterAgentConfig {
	if x != nil {
		return x.ClusterAgentConfig
	}
	return nil
}

func (x *DeploymentConfig) GetHostAgentTemplate() *HostAgentConfig {
	if x != nil {
		return x.HostAgentTemplate
	}
	return nil
}

func (x *DeploymentConfig) GetTestlopriInstances() []*DeployedTestLopriInstance {
	if x != nil {
		return x.TestlopriInstances
	}
	return nil
}

func (x *DeploymentConfig) GetFortioGroups() []*DeployedFortioGroup {
	if x != nil {
		return x.FortioGroups
	}
	return nil
}

func (x *DeploymentConfig) GetFortioInstances() []*DeployedFortioInstance {
	if x != nil {
		return x.FortioInstances
	}
	return nil
}

var File_heyp_proto_deployment_proto protoreflect.FileDescriptor

var file_heyp_proto_deployment_proto_rawDesc = []byte{
	0x0a, 0x1b, 0x68, 0x65, 0x79, 0x70, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x64, 0x65, 0x70,
	0x6c, 0x6f, 0x79, 0x6d, 0x65, 0x6e, 0x74, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x0a, 0x68,
	0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x14, 0x68, 0x65, 0x79, 0x70, 0x2f,
	0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x61, 0x70, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a,
	0x17, 0x68, 0x65, 0x79, 0x70, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x6f, 0x6e, 0x66,
	0x69, 0x67, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x15, 0x68, 0x65, 0x79, 0x70, 0x2f, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22,
	0x86, 0x01, 0x0a, 0x0c, 0x44, 0x65, 0x70, 0x6c, 0x6f, 0x79, 0x65, 0x64, 0x4e, 0x6f, 0x64, 0x65,
	0x12, 0x12, 0x0a, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x04,
	0x6e, 0x61, 0x6d, 0x65, 0x12, 0x23, 0x0a, 0x0d, 0x65, 0x78, 0x74, 0x65, 0x72, 0x6e, 0x61, 0x6c,
	0x5f, 0x61, 0x64, 0x64, 0x72, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x0c, 0x65, 0x78, 0x74,
	0x65, 0x72, 0x6e, 0x61, 0x6c, 0x41, 0x64, 0x64, 0x72, 0x12, 0x27, 0x0a, 0x0f, 0x65, 0x78, 0x70,
	0x65, 0x72, 0x69, 0x6d, 0x65, 0x6e, 0x74, 0x5f, 0x61, 0x64, 0x64, 0x72, 0x18, 0x03, 0x20, 0x01,
	0x28, 0x09, 0x52, 0x0e, 0x65, 0x78, 0x70, 0x65, 0x72, 0x69, 0x6d, 0x65, 0x6e, 0x74, 0x41, 0x64,
	0x64, 0x72, 0x12, 0x14, 0x0a, 0x05, 0x72, 0x6f, 0x6c, 0x65, 0x73, 0x18, 0x0a, 0x20, 0x03, 0x28,
	0x09, 0x52, 0x05, 0x72, 0x6f, 0x6c, 0x65, 0x73, 0x22, 0xa3, 0x01, 0x0a, 0x0f, 0x44, 0x65, 0x70,
	0x6c, 0x6f, 0x79, 0x65, 0x64, 0x43, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x12, 0x12, 0x0a, 0x04,
	0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x04, 0x6e, 0x61, 0x6d, 0x65,
	0x12, 0x1d, 0x0a, 0x0a, 0x6e, 0x6f, 0x64, 0x65, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x73, 0x18, 0x02,
	0x20, 0x03, 0x28, 0x09, 0x52, 0x09, 0x6e, 0x6f, 0x64, 0x65, 0x4e, 0x61, 0x6d, 0x65, 0x73, 0x12,
	0x2f, 0x0a, 0x06, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x73, 0x18, 0x03, 0x20, 0x01, 0x28, 0x0b, 0x32,
	0x17, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x41, 0x6c, 0x6c,
	0x6f, 0x63, 0x42, 0x75, 0x6e, 0x64, 0x6c, 0x65, 0x52, 0x06, 0x6c, 0x69, 0x6d, 0x69, 0x74, 0x73,
	0x12, 0x2c, 0x0a, 0x12, 0x63, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x5f, 0x61, 0x67, 0x65, 0x6e,
	0x74, 0x5f, 0x70, 0x6f, 0x72, 0x74, 0x18, 0x04, 0x20, 0x01, 0x28, 0x05, 0x52, 0x10, 0x63, 0x6c,
	0x75, 0x73, 0x74, 0x65, 0x72, 0x41, 0x67, 0x65, 0x6e, 0x74, 0x50, 0x6f, 0x72, 0x74, 0x22, 0xe7,
	0x01, 0x0a, 0x19, 0x44, 0x65, 0x70, 0x6c, 0x6f, 0x79, 0x65, 0x64, 0x54, 0x65, 0x73, 0x74, 0x4c,
	0x6f, 0x70, 0x72, 0x69, 0x49, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x12, 0x12, 0x0a, 0x04,
	0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x04, 0x6e, 0x61, 0x6d, 0x65,
	0x12, 0x1d, 0x0a, 0x0a, 0x73, 0x65, 0x72, 0x76, 0x65, 0x5f, 0x70, 0x6f, 0x72, 0x74, 0x18, 0x02,
	0x20, 0x01, 0x28, 0x05, 0x52, 0x09, 0x73, 0x65, 0x72, 0x76, 0x65, 0x50, 0x6f, 0x72, 0x74, 0x12,
	0x39, 0x0a, 0x06, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x18, 0x03, 0x20, 0x01, 0x28, 0x0b, 0x32,
	0x21, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x65, 0x73,
	0x74, 0x4c, 0x6f, 0x70, 0x72, 0x69, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x43, 0x6f, 0x6e, 0x66,
	0x69, 0x67, 0x52, 0x06, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x12, 0x2d, 0x0a, 0x11, 0x6e, 0x75,
	0x6d, 0x5f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x5f, 0x73, 0x68, 0x61, 0x72, 0x64, 0x73, 0x18,
	0x04, 0x20, 0x01, 0x28, 0x05, 0x3a, 0x01, 0x31, 0x52, 0x0f, 0x6e, 0x75, 0x6d, 0x43, 0x6c, 0x69,
	0x65, 0x6e, 0x74, 0x53, 0x68, 0x61, 0x72, 0x64, 0x73, 0x12, 0x2d, 0x0a, 0x11, 0x6e, 0x75, 0x6d,
	0x5f, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x5f, 0x73, 0x68, 0x61, 0x72, 0x64, 0x73, 0x18, 0x05,
	0x20, 0x01, 0x28, 0x05, 0x3a, 0x01, 0x31, 0x52, 0x0f, 0x6e, 0x75, 0x6d, 0x53, 0x65, 0x72, 0x76,
	0x65, 0x72, 0x53, 0x68, 0x61, 0x72, 0x64, 0x73, 0x22, 0xc9, 0x01, 0x0a, 0x16, 0x44, 0x65, 0x70,
	0x6c, 0x6f, 0x79, 0x65, 0x64, 0x46, 0x6f, 0x72, 0x74, 0x69, 0x6f, 0x49, 0x6e, 0x73, 0x74, 0x61,
	0x6e, 0x63, 0x65, 0x12, 0x14, 0x0a, 0x05, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x18, 0x01, 0x20, 0x01,
	0x28, 0x09, 0x52, 0x05, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x12, 0x12, 0x0a, 0x04, 0x6e, 0x61, 0x6d,
	0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x12, 0x23, 0x0a,
	0x0a, 0x73, 0x65, 0x72, 0x76, 0x65, 0x5f, 0x70, 0x6f, 0x72, 0x74, 0x18, 0x03, 0x20, 0x01, 0x28,
	0x05, 0x3a, 0x04, 0x39, 0x39, 0x31, 0x31, 0x52, 0x09, 0x73, 0x65, 0x72, 0x76, 0x65, 0x50, 0x6f,
	0x72, 0x74, 0x12, 0x28, 0x0a, 0x09, 0x6c, 0x62, 0x5f, 0x70, 0x6f, 0x6c, 0x69, 0x63, 0x79, 0x18,
	0x04, 0x20, 0x01, 0x28, 0x09, 0x3a, 0x0b, 0x52, 0x4f, 0x55, 0x4e, 0x44, 0x5f, 0x52, 0x4f, 0x42,
	0x49, 0x4e, 0x52, 0x08, 0x6c, 0x62, 0x50, 0x6f, 0x6c, 0x69, 0x63, 0x79, 0x12, 0x36, 0x0a, 0x06,
	0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x18, 0x05, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x1e, 0x2e, 0x68,
	0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x46, 0x6f, 0x72, 0x74, 0x69, 0x6f,
	0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x52, 0x06, 0x63, 0x6c,
	0x69, 0x65, 0x6e, 0x74, 0x22, 0x72, 0x0a, 0x13, 0x44, 0x65, 0x70, 0x6c, 0x6f, 0x79, 0x65, 0x64,
	0x46, 0x6f, 0x72, 0x74, 0x69, 0x6f, 0x47, 0x72, 0x6f, 0x75, 0x70, 0x12, 0x12, 0x0a, 0x04, 0x6e,
	0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x12,
	0x1d, 0x0a, 0x0a, 0x65, 0x6e, 0x76, 0x6f, 0x79, 0x5f, 0x70, 0x6f, 0x72, 0x74, 0x18, 0x02, 0x20,
	0x01, 0x28, 0x05, 0x52, 0x09, 0x65, 0x6e, 0x76, 0x6f, 0x79, 0x50, 0x6f, 0x72, 0x74, 0x12, 0x28,
	0x0a, 0x10, 0x65, 0x6e, 0x76, 0x6f, 0x79, 0x5f, 0x61, 0x64, 0x6d, 0x69, 0x6e, 0x5f, 0x70, 0x6f,
	0x72, 0x74, 0x18, 0x03, 0x20, 0x01, 0x28, 0x05, 0x52, 0x0e, 0x65, 0x6e, 0x76, 0x6f, 0x79, 0x41,
	0x64, 0x6d, 0x69, 0x6e, 0x50, 0x6f, 0x72, 0x74, 0x22, 0x87, 0x04, 0x0a, 0x10, 0x44, 0x65, 0x70,
	0x6c, 0x6f, 0x79, 0x6d, 0x65, 0x6e, 0x74, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x12, 0x2e, 0x0a,
	0x05, 0x6e, 0x6f, 0x64, 0x65, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x18, 0x2e, 0x68,
	0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x44, 0x65, 0x70, 0x6c, 0x6f, 0x79,
	0x65, 0x64, 0x4e, 0x6f, 0x64, 0x65, 0x52, 0x05, 0x6e, 0x6f, 0x64, 0x65, 0x73, 0x12, 0x37, 0x0a,
	0x08, 0x63, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x73, 0x18, 0x02, 0x20, 0x03, 0x28, 0x0b, 0x32,
	0x1b, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x44, 0x65, 0x70,
	0x6c, 0x6f, 0x79, 0x65, 0x64, 0x43, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x52, 0x08, 0x63, 0x6c,
	0x75, 0x73, 0x74, 0x65, 0x72, 0x73, 0x12, 0x50, 0x0a, 0x14, 0x63, 0x6c, 0x75, 0x73, 0x74, 0x65,
	0x72, 0x5f, 0x61, 0x67, 0x65, 0x6e, 0x74, 0x5f, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x18, 0x0a,
	0x20, 0x01, 0x28, 0x0b, 0x32, 0x1e, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2e, 0x43, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x41, 0x67, 0x65, 0x6e, 0x74, 0x43, 0x6f,
	0x6e, 0x66, 0x69, 0x67, 0x52, 0x12, 0x63, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x41, 0x67, 0x65,
	0x6e, 0x74, 0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x12, 0x4b, 0x0a, 0x13, 0x68, 0x6f, 0x73, 0x74,
	0x5f, 0x61, 0x67, 0x65, 0x6e, 0x74, 0x5f, 0x74, 0x65, 0x6d, 0x70, 0x6c, 0x61, 0x74, 0x65, 0x18,
	0x0b, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x1b, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x2e, 0x48, 0x6f, 0x73, 0x74, 0x41, 0x67, 0x65, 0x6e, 0x74, 0x43, 0x6f, 0x6e, 0x66,
	0x69, 0x67, 0x52, 0x11, 0x68, 0x6f, 0x73, 0x74, 0x41, 0x67, 0x65, 0x6e, 0x74, 0x54, 0x65, 0x6d,
	0x70, 0x6c, 0x61, 0x74, 0x65, 0x12, 0x56, 0x0a, 0x13, 0x74, 0x65, 0x73, 0x74, 0x6c, 0x6f, 0x70,
	0x72, 0x69, 0x5f, 0x69, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x73, 0x18, 0x0c, 0x20, 0x03,
	0x28, 0x0b, 0x32, 0x25, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e,
	0x44, 0x65, 0x70, 0x6c, 0x6f, 0x79, 0x65, 0x64, 0x54, 0x65, 0x73, 0x74, 0x4c, 0x6f, 0x70, 0x72,
	0x69, 0x49, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x52, 0x12, 0x74, 0x65, 0x73, 0x74, 0x6c,
	0x6f, 0x70, 0x72, 0x69, 0x49, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x73, 0x12, 0x44, 0x0a,
	0x0d, 0x66, 0x6f, 0x72, 0x74, 0x69, 0x6f, 0x5f, 0x67, 0x72, 0x6f, 0x75, 0x70, 0x73, 0x18, 0x0d,
	0x20, 0x03, 0x28, 0x0b, 0x32, 0x1f, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2e, 0x44, 0x65, 0x70, 0x6c, 0x6f, 0x79, 0x65, 0x64, 0x46, 0x6f, 0x72, 0x74, 0x69, 0x6f,
	0x47, 0x72, 0x6f, 0x75, 0x70, 0x52, 0x0c, 0x66, 0x6f, 0x72, 0x74, 0x69, 0x6f, 0x47, 0x72, 0x6f,
	0x75, 0x70, 0x73, 0x12, 0x4d, 0x0a, 0x10, 0x66, 0x6f, 0x72, 0x74, 0x69, 0x6f, 0x5f, 0x69, 0x6e,
	0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x73, 0x18, 0x0e, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x22, 0x2e,
	0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x44, 0x65, 0x70, 0x6c, 0x6f,
	0x79, 0x65, 0x64, 0x46, 0x6f, 0x72, 0x74, 0x69, 0x6f, 0x49, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x63,
	0x65, 0x52, 0x0f, 0x66, 0x6f, 0x72, 0x74, 0x69, 0x6f, 0x49, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x63,
	0x65, 0x73, 0x42, 0x28, 0x5a, 0x26, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d,
	0x2f, 0x75, 0x6c, 0x75, 0x79, 0x6f, 0x6c, 0x2f, 0x68, 0x65, 0x79, 0x70, 0x2d, 0x61, 0x67, 0x65,
	0x6e, 0x74, 0x73, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f,
}

var (
	file_heyp_proto_deployment_proto_rawDescOnce sync.Once
	file_heyp_proto_deployment_proto_rawDescData = file_heyp_proto_deployment_proto_rawDesc
)

func file_heyp_proto_deployment_proto_rawDescGZIP() []byte {
	file_heyp_proto_deployment_proto_rawDescOnce.Do(func() {
		file_heyp_proto_deployment_proto_rawDescData = protoimpl.X.CompressGZIP(file_heyp_proto_deployment_proto_rawDescData)
	})
	return file_heyp_proto_deployment_proto_rawDescData
}

var file_heyp_proto_deployment_proto_msgTypes = make([]protoimpl.MessageInfo, 6)
var file_heyp_proto_deployment_proto_goTypes = []interface{}{
	(*DeployedNode)(nil),              // 0: heyp.proto.DeployedNode
	(*DeployedCluster)(nil),           // 1: heyp.proto.DeployedCluster
	(*DeployedTestLopriInstance)(nil), // 2: heyp.proto.DeployedTestLopriInstance
	(*DeployedFortioInstance)(nil),    // 3: heyp.proto.DeployedFortioInstance
	(*DeployedFortioGroup)(nil),       // 4: heyp.proto.DeployedFortioGroup
	(*DeploymentConfig)(nil),          // 5: heyp.proto.DeploymentConfig
	(*AllocBundle)(nil),               // 6: heyp.proto.AllocBundle
	(*TestLopriClientConfig)(nil),     // 7: heyp.proto.TestLopriClientConfig
	(*FortioClientConfig)(nil),        // 8: heyp.proto.FortioClientConfig
	(*ClusterAgentConfig)(nil),        // 9: heyp.proto.ClusterAgentConfig
	(*HostAgentConfig)(nil),           // 10: heyp.proto.HostAgentConfig
}
var file_heyp_proto_deployment_proto_depIdxs = []int32{
	6,  // 0: heyp.proto.DeployedCluster.limits:type_name -> heyp.proto.AllocBundle
	7,  // 1: heyp.proto.DeployedTestLopriInstance.client:type_name -> heyp.proto.TestLopriClientConfig
	8,  // 2: heyp.proto.DeployedFortioInstance.client:type_name -> heyp.proto.FortioClientConfig
	0,  // 3: heyp.proto.DeploymentConfig.nodes:type_name -> heyp.proto.DeployedNode
	1,  // 4: heyp.proto.DeploymentConfig.clusters:type_name -> heyp.proto.DeployedCluster
	9,  // 5: heyp.proto.DeploymentConfig.cluster_agent_config:type_name -> heyp.proto.ClusterAgentConfig
	10, // 6: heyp.proto.DeploymentConfig.host_agent_template:type_name -> heyp.proto.HostAgentConfig
	2,  // 7: heyp.proto.DeploymentConfig.testlopri_instances:type_name -> heyp.proto.DeployedTestLopriInstance
	4,  // 8: heyp.proto.DeploymentConfig.fortio_groups:type_name -> heyp.proto.DeployedFortioGroup
	3,  // 9: heyp.proto.DeploymentConfig.fortio_instances:type_name -> heyp.proto.DeployedFortioInstance
	10, // [10:10] is the sub-list for method output_type
	10, // [10:10] is the sub-list for method input_type
	10, // [10:10] is the sub-list for extension type_name
	10, // [10:10] is the sub-list for extension extendee
	0,  // [0:10] is the sub-list for field type_name
}

func init() { file_heyp_proto_deployment_proto_init() }
func file_heyp_proto_deployment_proto_init() {
	if File_heyp_proto_deployment_proto != nil {
		return
	}
	file_heyp_proto_app_proto_init()
	file_heyp_proto_config_proto_init()
	file_heyp_proto_heyp_proto_init()
	if !protoimpl.UnsafeEnabled {
		file_heyp_proto_deployment_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*DeployedNode); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_heyp_proto_deployment_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*DeployedCluster); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_heyp_proto_deployment_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*DeployedTestLopriInstance); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_heyp_proto_deployment_proto_msgTypes[3].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*DeployedFortioInstance); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_heyp_proto_deployment_proto_msgTypes[4].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*DeployedFortioGroup); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_heyp_proto_deployment_proto_msgTypes[5].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*DeploymentConfig); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_heyp_proto_deployment_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   6,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_heyp_proto_deployment_proto_goTypes,
		DependencyIndexes: file_heyp_proto_deployment_proto_depIdxs,
		MessageInfos:      file_heyp_proto_deployment_proto_msgTypes,
	}.Build()
	File_heyp_proto_deployment_proto = out.File
	file_heyp_proto_deployment_proto_rawDesc = nil
	file_heyp_proto_deployment_proto_goTypes = nil
	file_heyp_proto_deployment_proto_depIdxs = nil
}
