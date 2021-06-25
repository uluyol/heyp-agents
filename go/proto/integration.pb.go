// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.15.6
// source: heyp/proto/integration.proto

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

type TestCompareMetrics struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Metrics []*TestCompareMetrics_Metric `protobuf:"bytes,1,rep,name=metrics,proto3" json:"metrics,omitempty"`
}

func (x *TestCompareMetrics) Reset() {
	*x = TestCompareMetrics{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_integration_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TestCompareMetrics) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TestCompareMetrics) ProtoMessage() {}

func (x *TestCompareMetrics) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_integration_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TestCompareMetrics.ProtoReflect.Descriptor instead.
func (*TestCompareMetrics) Descriptor() ([]byte, []int) {
	return file_heyp_proto_integration_proto_rawDescGZIP(), []int{0}
}

func (x *TestCompareMetrics) GetMetrics() []*TestCompareMetrics_Metric {
	if x != nil {
		return x.Metrics
	}
	return nil
}

type TestCompareMetrics_Metric struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Name  string  `protobuf:"bytes,1,opt,name=name,proto3" json:"name,omitempty"`
	Value float64 `protobuf:"fixed64,2,opt,name=value,proto3" json:"value,omitempty"`
}

func (x *TestCompareMetrics_Metric) Reset() {
	*x = TestCompareMetrics_Metric{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_integration_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TestCompareMetrics_Metric) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TestCompareMetrics_Metric) ProtoMessage() {}

func (x *TestCompareMetrics_Metric) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_integration_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TestCompareMetrics_Metric.ProtoReflect.Descriptor instead.
func (*TestCompareMetrics_Metric) Descriptor() ([]byte, []int) {
	return file_heyp_proto_integration_proto_rawDescGZIP(), []int{0, 0}
}

func (x *TestCompareMetrics_Metric) GetName() string {
	if x != nil {
		return x.Name
	}
	return ""
}

func (x *TestCompareMetrics_Metric) GetValue() float64 {
	if x != nil {
		return x.Value
	}
	return 0
}

var File_heyp_proto_integration_proto protoreflect.FileDescriptor

var file_heyp_proto_integration_proto_rawDesc = []byte{
	0x0a, 0x1c, 0x68, 0x65, 0x79, 0x70, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x69, 0x6e, 0x74,
	0x65, 0x67, 0x72, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x0a,
	0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x89, 0x01, 0x0a, 0x12, 0x54,
	0x65, 0x73, 0x74, 0x43, 0x6f, 0x6d, 0x70, 0x61, 0x72, 0x65, 0x4d, 0x65, 0x74, 0x72, 0x69, 0x63,
	0x73, 0x12, 0x3f, 0x0a, 0x07, 0x6d, 0x65, 0x74, 0x72, 0x69, 0x63, 0x73, 0x18, 0x01, 0x20, 0x03,
	0x28, 0x0b, 0x32, 0x25, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e,
	0x54, 0x65, 0x73, 0x74, 0x43, 0x6f, 0x6d, 0x70, 0x61, 0x72, 0x65, 0x4d, 0x65, 0x74, 0x72, 0x69,
	0x63, 0x73, 0x2e, 0x4d, 0x65, 0x74, 0x72, 0x69, 0x63, 0x52, 0x07, 0x6d, 0x65, 0x74, 0x72, 0x69,
	0x63, 0x73, 0x1a, 0x32, 0x0a, 0x06, 0x4d, 0x65, 0x74, 0x72, 0x69, 0x63, 0x12, 0x12, 0x0a, 0x04,
	0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x04, 0x6e, 0x61, 0x6d, 0x65,
	0x12, 0x14, 0x0a, 0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x01, 0x52,
	0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x42, 0x28, 0x5a, 0x26, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62,
	0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x75, 0x6c, 0x75, 0x79, 0x6f, 0x6c, 0x2f, 0x68, 0x65, 0x79, 0x70,
	0x2d, 0x61, 0x67, 0x65, 0x6e, 0x74, 0x73, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x62, 0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_heyp_proto_integration_proto_rawDescOnce sync.Once
	file_heyp_proto_integration_proto_rawDescData = file_heyp_proto_integration_proto_rawDesc
)

func file_heyp_proto_integration_proto_rawDescGZIP() []byte {
	file_heyp_proto_integration_proto_rawDescOnce.Do(func() {
		file_heyp_proto_integration_proto_rawDescData = protoimpl.X.CompressGZIP(file_heyp_proto_integration_proto_rawDescData)
	})
	return file_heyp_proto_integration_proto_rawDescData
}

var file_heyp_proto_integration_proto_msgTypes = make([]protoimpl.MessageInfo, 2)
var file_heyp_proto_integration_proto_goTypes = []interface{}{
	(*TestCompareMetrics)(nil),        // 0: heyp.proto.TestCompareMetrics
	(*TestCompareMetrics_Metric)(nil), // 1: heyp.proto.TestCompareMetrics.Metric
}
var file_heyp_proto_integration_proto_depIdxs = []int32{
	1, // 0: heyp.proto.TestCompareMetrics.metrics:type_name -> heyp.proto.TestCompareMetrics.Metric
	1, // [1:1] is the sub-list for method output_type
	1, // [1:1] is the sub-list for method input_type
	1, // [1:1] is the sub-list for extension type_name
	1, // [1:1] is the sub-list for extension extendee
	0, // [0:1] is the sub-list for field type_name
}

func init() { file_heyp_proto_integration_proto_init() }
func file_heyp_proto_integration_proto_init() {
	if File_heyp_proto_integration_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_heyp_proto_integration_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TestCompareMetrics); i {
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
		file_heyp_proto_integration_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TestCompareMetrics_Metric); i {
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
			RawDescriptor: file_heyp_proto_integration_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   2,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_heyp_proto_integration_proto_goTypes,
		DependencyIndexes: file_heyp_proto_integration_proto_depIdxs,
		MessageInfos:      file_heyp_proto_integration_proto_msgTypes,
	}.Build()
	File_heyp_proto_integration_proto = out.File
	file_heyp_proto_integration_proto_rawDesc = nil
	file_heyp_proto_integration_proto_goTypes = nil
	file_heyp_proto_integration_proto_depIdxs = nil
}
