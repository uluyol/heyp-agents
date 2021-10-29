// Code generated by protoc-gen-go-grpc. DO NOT EDIT.

package pb

import (
	context "context"
	grpc "google.golang.org/grpc"
	codes "google.golang.org/grpc/codes"
	status "google.golang.org/grpc/status"
)

// This is a compile-time assertion to ensure that this generated file
// is compatible with the grpc package it is being compiled against.
// Requires gRPC-Go v1.32.0 or later.
const _ = grpc.SupportPackageIsVersion7

// ClusterAgentClient is the client API for ClusterAgent service.
//
// For semantics around ctx use and closing/ending streaming RPCs, please refer to https://pkg.go.dev/google.golang.org/grpc/?tab=doc#ClientConn.NewStream.
type ClusterAgentClient interface {
	RegisterHost(ctx context.Context, opts ...grpc.CallOption) (ClusterAgent_RegisterHostClient, error)
}

type clusterAgentClient struct {
	cc grpc.ClientConnInterface
}

func NewClusterAgentClient(cc grpc.ClientConnInterface) ClusterAgentClient {
	return &clusterAgentClient{cc}
}

func (c *clusterAgentClient) RegisterHost(ctx context.Context, opts ...grpc.CallOption) (ClusterAgent_RegisterHostClient, error) {
	stream, err := c.cc.NewStream(ctx, &ClusterAgent_ServiceDesc.Streams[0], "/heyp.proto.ClusterAgent/RegisterHost", opts...)
	if err != nil {
		return nil, err
	}
	x := &clusterAgentRegisterHostClient{stream}
	return x, nil
}

type ClusterAgent_RegisterHostClient interface {
	Send(*InfoBundle) error
	Recv() (*AllocBundle, error)
	grpc.ClientStream
}

type clusterAgentRegisterHostClient struct {
	grpc.ClientStream
}

func (x *clusterAgentRegisterHostClient) Send(m *InfoBundle) error {
	return x.ClientStream.SendMsg(m)
}

func (x *clusterAgentRegisterHostClient) Recv() (*AllocBundle, error) {
	m := new(AllocBundle)
	if err := x.ClientStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

// ClusterAgentServer is the server API for ClusterAgent service.
// All implementations must embed UnimplementedClusterAgentServer
// for forward compatibility
type ClusterAgentServer interface {
	RegisterHost(ClusterAgent_RegisterHostServer) error
	mustEmbedUnimplementedClusterAgentServer()
}

// UnimplementedClusterAgentServer must be embedded to have forward compatible implementations.
type UnimplementedClusterAgentServer struct {
}

func (UnimplementedClusterAgentServer) RegisterHost(ClusterAgent_RegisterHostServer) error {
	return status.Errorf(codes.Unimplemented, "method RegisterHost not implemented")
}
func (UnimplementedClusterAgentServer) mustEmbedUnimplementedClusterAgentServer() {}

// UnsafeClusterAgentServer may be embedded to opt out of forward compatibility for this service.
// Use of this interface is not recommended, as added methods to ClusterAgentServer will
// result in compilation errors.
type UnsafeClusterAgentServer interface {
	mustEmbedUnimplementedClusterAgentServer()
}

func RegisterClusterAgentServer(s grpc.ServiceRegistrar, srv ClusterAgentServer) {
	s.RegisterService(&ClusterAgent_ServiceDesc, srv)
}

func _ClusterAgent_RegisterHost_Handler(srv interface{}, stream grpc.ServerStream) error {
	return srv.(ClusterAgentServer).RegisterHost(&clusterAgentRegisterHostServer{stream})
}

type ClusterAgent_RegisterHostServer interface {
	Send(*AllocBundle) error
	Recv() (*InfoBundle, error)
	grpc.ServerStream
}

type clusterAgentRegisterHostServer struct {
	grpc.ServerStream
}

func (x *clusterAgentRegisterHostServer) Send(m *AllocBundle) error {
	return x.ServerStream.SendMsg(m)
}

func (x *clusterAgentRegisterHostServer) Recv() (*InfoBundle, error) {
	m := new(InfoBundle)
	if err := x.ServerStream.RecvMsg(m); err != nil {
		return nil, err
	}
	return m, nil
}

// ClusterAgent_ServiceDesc is the grpc.ServiceDesc for ClusterAgent service.
// It's only intended for direct use with grpc.RegisterService,
// and not to be introspected or modified (even as a copy)
var ClusterAgent_ServiceDesc = grpc.ServiceDesc{
	ServiceName: "heyp.proto.ClusterAgent",
	HandlerType: (*ClusterAgentServer)(nil),
	Methods:     []grpc.MethodDesc{},
	Streams: []grpc.StreamDesc{
		{
			StreamName:    "RegisterHost",
			Handler:       _ClusterAgent_RegisterHost_Handler,
			ServerStreams: true,
			ClientStreams: true,
		},
	},
	Metadata: "heyp/proto/heyp.proto",
}