// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.15.6
// source: heyp/proto/stats.proto

package pb

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

type HdrHistogram struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Config  *HdrHistogram_Config   `protobuf:"bytes,1,opt,name=config,proto3" json:"config,omitempty"`
	Buckets []*HdrHistogram_Bucket `protobuf:"bytes,2,rep,name=buckets,proto3" json:"buckets,omitempty"`
}

func (x *HdrHistogram) Reset() {
	*x = HdrHistogram{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_stats_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *HdrHistogram) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*HdrHistogram) ProtoMessage() {}

func (x *HdrHistogram) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_stats_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use HdrHistogram.ProtoReflect.Descriptor instead.
func (*HdrHistogram) Descriptor() ([]byte, []int) {
	return file_heyp_proto_stats_proto_rawDescGZIP(), []int{0}
}

func (x *HdrHistogram) GetConfig() *HdrHistogram_Config {
	if x != nil {
		return x.Config
	}
	return nil
}

func (x *HdrHistogram) GetBuckets() []*HdrHistogram_Bucket {
	if x != nil {
		return x.Buckets
	}
	return nil
}

type StatsRecord struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Label     string  `protobuf:"bytes,1,opt,name=label,proto3" json:"label,omitempty"`
	Timestamp string  `protobuf:"bytes,2,opt,name=timestamp,proto3" json:"timestamp,omitempty"` // ISO 8601
	DurSec    float64 `protobuf:"fixed64,3,opt,name=dur_sec,json=durSec,proto3" json:"dur_sec,omitempty"`
	// cumulative stats
	CumNumBits int64 `protobuf:"varint,10,opt,name=cum_num_bits,json=cumNumBits,proto3" json:"cum_num_bits,omitempty"`
	CumNumRpcs int64 `protobuf:"varint,11,opt,name=cum_num_rpcs,json=cumNumRpcs,proto3" json:"cum_num_rpcs,omitempty"`
	// current stats
	MeanBitsPerSec float64                     `protobuf:"fixed64,21,opt,name=mean_bits_per_sec,json=meanBitsPerSec,proto3" json:"mean_bits_per_sec,omitempty"`
	MeanRpcsPerSec float64                     `protobuf:"fixed64,22,opt,name=mean_rpcs_per_sec,json=meanRpcsPerSec,proto3" json:"mean_rpcs_per_sec,omitempty"`
	Latency        []*StatsRecord_LatencyStats `protobuf:"bytes,23,rep,name=latency,proto3" json:"latency,omitempty"`
}

func (x *StatsRecord) Reset() {
	*x = StatsRecord{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_stats_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *StatsRecord) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*StatsRecord) ProtoMessage() {}

func (x *StatsRecord) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_stats_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use StatsRecord.ProtoReflect.Descriptor instead.
func (*StatsRecord) Descriptor() ([]byte, []int) {
	return file_heyp_proto_stats_proto_rawDescGZIP(), []int{1}
}

func (x *StatsRecord) GetLabel() string {
	if x != nil {
		return x.Label
	}
	return ""
}

func (x *StatsRecord) GetTimestamp() string {
	if x != nil {
		return x.Timestamp
	}
	return ""
}

func (x *StatsRecord) GetDurSec() float64 {
	if x != nil {
		return x.DurSec
	}
	return 0
}

func (x *StatsRecord) GetCumNumBits() int64 {
	if x != nil {
		return x.CumNumBits
	}
	return 0
}

func (x *StatsRecord) GetCumNumRpcs() int64 {
	if x != nil {
		return x.CumNumRpcs
	}
	return 0
}

func (x *StatsRecord) GetMeanBitsPerSec() float64 {
	if x != nil {
		return x.MeanBitsPerSec
	}
	return 0
}

func (x *StatsRecord) GetMeanRpcsPerSec() float64 {
	if x != nil {
		return x.MeanRpcsPerSec
	}
	return 0
}

func (x *StatsRecord) GetLatency() []*StatsRecord_LatencyStats {
	if x != nil {
		return x.Latency
	}
	return nil
}

type HdrHistogram_Config struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	LowestDiscernibleValue int64 `protobuf:"varint,1,opt,name=lowest_discernible_value,json=lowestDiscernibleValue,proto3" json:"lowest_discernible_value,omitempty"`
	HighestTrackableValue  int64 `protobuf:"varint,2,opt,name=highest_trackable_value,json=highestTrackableValue,proto3" json:"highest_trackable_value,omitempty"`
	SignificantFigures     int32 `protobuf:"varint,3,opt,name=significant_figures,json=significantFigures,proto3" json:"significant_figures,omitempty"`
}

func (x *HdrHistogram_Config) Reset() {
	*x = HdrHistogram_Config{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_stats_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *HdrHistogram_Config) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*HdrHistogram_Config) ProtoMessage() {}

func (x *HdrHistogram_Config) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_stats_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use HdrHistogram_Config.ProtoReflect.Descriptor instead.
func (*HdrHistogram_Config) Descriptor() ([]byte, []int) {
	return file_heyp_proto_stats_proto_rawDescGZIP(), []int{0, 0}
}

func (x *HdrHistogram_Config) GetLowestDiscernibleValue() int64 {
	if x != nil {
		return x.LowestDiscernibleValue
	}
	return 0
}

func (x *HdrHistogram_Config) GetHighestTrackableValue() int64 {
	if x != nil {
		return x.HighestTrackableValue
	}
	return 0
}

func (x *HdrHistogram_Config) GetSignificantFigures() int32 {
	if x != nil {
		return x.SignificantFigures
	}
	return 0
}

type HdrHistogram_Bucket struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	V int64 `protobuf:"varint,1,opt,name=v,proto3" json:"v,omitempty"` // value
	C int64 `protobuf:"varint,2,opt,name=c,proto3" json:"c,omitempty"` // count
}

func (x *HdrHistogram_Bucket) Reset() {
	*x = HdrHistogram_Bucket{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_stats_proto_msgTypes[3]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *HdrHistogram_Bucket) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*HdrHistogram_Bucket) ProtoMessage() {}

func (x *HdrHistogram_Bucket) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_stats_proto_msgTypes[3]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use HdrHistogram_Bucket.ProtoReflect.Descriptor instead.
func (*HdrHistogram_Bucket) Descriptor() ([]byte, []int) {
	return file_heyp_proto_stats_proto_rawDescGZIP(), []int{0, 1}
}

func (x *HdrHistogram_Bucket) GetV() int64 {
	if x != nil {
		return x.V
	}
	return 0
}

func (x *HdrHistogram_Bucket) GetC() int64 {
	if x != nil {
		return x.C
	}
	return 0
}

type StatsRecord_LatencyStats struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Kind   string        `protobuf:"bytes,1,opt,name=kind,proto3" json:"kind,omitempty"`
	HistNs *HdrHistogram `protobuf:"bytes,2,opt,name=hist_ns,json=histNs,proto3" json:"hist_ns,omitempty"`
	// convenience percentiles
	P50Ns int64 `protobuf:"varint,11,opt,name=p50_ns,json=p50Ns,proto3" json:"p50_ns,omitempty"`
	P90Ns int64 `protobuf:"varint,12,opt,name=p90_ns,json=p90Ns,proto3" json:"p90_ns,omitempty"`
	P95Ns int64 `protobuf:"varint,13,opt,name=p95_ns,json=p95Ns,proto3" json:"p95_ns,omitempty"`
	P99Ns int64 `protobuf:"varint,14,opt,name=p99_ns,json=p99Ns,proto3" json:"p99_ns,omitempty"`
}

func (x *StatsRecord_LatencyStats) Reset() {
	*x = StatsRecord_LatencyStats{}
	if protoimpl.UnsafeEnabled {
		mi := &file_heyp_proto_stats_proto_msgTypes[4]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *StatsRecord_LatencyStats) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*StatsRecord_LatencyStats) ProtoMessage() {}

func (x *StatsRecord_LatencyStats) ProtoReflect() protoreflect.Message {
	mi := &file_heyp_proto_stats_proto_msgTypes[4]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use StatsRecord_LatencyStats.ProtoReflect.Descriptor instead.
func (*StatsRecord_LatencyStats) Descriptor() ([]byte, []int) {
	return file_heyp_proto_stats_proto_rawDescGZIP(), []int{1, 0}
}

func (x *StatsRecord_LatencyStats) GetKind() string {
	if x != nil {
		return x.Kind
	}
	return ""
}

func (x *StatsRecord_LatencyStats) GetHistNs() *HdrHistogram {
	if x != nil {
		return x.HistNs
	}
	return nil
}

func (x *StatsRecord_LatencyStats) GetP50Ns() int64 {
	if x != nil {
		return x.P50Ns
	}
	return 0
}

func (x *StatsRecord_LatencyStats) GetP90Ns() int64 {
	if x != nil {
		return x.P90Ns
	}
	return 0
}

func (x *StatsRecord_LatencyStats) GetP95Ns() int64 {
	if x != nil {
		return x.P95Ns
	}
	return 0
}

func (x *StatsRecord_LatencyStats) GetP99Ns() int64 {
	if x != nil {
		return x.P99Ns
	}
	return 0
}

var File_heyp_proto_stats_proto protoreflect.FileDescriptor

var file_heyp_proto_stats_proto_rawDesc = []byte{
	0x0a, 0x16, 0x68, 0x65, 0x79, 0x70, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x73, 0x74, 0x61,
	0x74, 0x73, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x0a, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x22, 0xd6, 0x02, 0x0a, 0x0c, 0x48, 0x64, 0x72, 0x48, 0x69, 0x73, 0x74,
	0x6f, 0x67, 0x72, 0x61, 0x6d, 0x12, 0x37, 0x0a, 0x06, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x18,
	0x01, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x1f, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x2e, 0x48, 0x64, 0x72, 0x48, 0x69, 0x73, 0x74, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x2e,
	0x43, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x52, 0x06, 0x63, 0x6f, 0x6e, 0x66, 0x69, 0x67, 0x12, 0x39,
	0x0a, 0x07, 0x62, 0x75, 0x63, 0x6b, 0x65, 0x74, 0x73, 0x18, 0x02, 0x20, 0x03, 0x28, 0x0b, 0x32,
	0x1f, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x48, 0x64, 0x72,
	0x48, 0x69, 0x73, 0x74, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x2e, 0x42, 0x75, 0x63, 0x6b, 0x65, 0x74,
	0x52, 0x07, 0x62, 0x75, 0x63, 0x6b, 0x65, 0x74, 0x73, 0x1a, 0xab, 0x01, 0x0a, 0x06, 0x43, 0x6f,
	0x6e, 0x66, 0x69, 0x67, 0x12, 0x38, 0x0a, 0x18, 0x6c, 0x6f, 0x77, 0x65, 0x73, 0x74, 0x5f, 0x64,
	0x69, 0x73, 0x63, 0x65, 0x72, 0x6e, 0x69, 0x62, 0x6c, 0x65, 0x5f, 0x76, 0x61, 0x6c, 0x75, 0x65,
	0x18, 0x01, 0x20, 0x01, 0x28, 0x03, 0x52, 0x16, 0x6c, 0x6f, 0x77, 0x65, 0x73, 0x74, 0x44, 0x69,
	0x73, 0x63, 0x65, 0x72, 0x6e, 0x69, 0x62, 0x6c, 0x65, 0x56, 0x61, 0x6c, 0x75, 0x65, 0x12, 0x36,
	0x0a, 0x17, 0x68, 0x69, 0x67, 0x68, 0x65, 0x73, 0x74, 0x5f, 0x74, 0x72, 0x61, 0x63, 0x6b, 0x61,
	0x62, 0x6c, 0x65, 0x5f, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x18, 0x02, 0x20, 0x01, 0x28, 0x03, 0x52,
	0x15, 0x68, 0x69, 0x67, 0x68, 0x65, 0x73, 0x74, 0x54, 0x72, 0x61, 0x63, 0x6b, 0x61, 0x62, 0x6c,
	0x65, 0x56, 0x61, 0x6c, 0x75, 0x65, 0x12, 0x2f, 0x0a, 0x13, 0x73, 0x69, 0x67, 0x6e, 0x69, 0x66,
	0x69, 0x63, 0x61, 0x6e, 0x74, 0x5f, 0x66, 0x69, 0x67, 0x75, 0x72, 0x65, 0x73, 0x18, 0x03, 0x20,
	0x01, 0x28, 0x05, 0x52, 0x12, 0x73, 0x69, 0x67, 0x6e, 0x69, 0x66, 0x69, 0x63, 0x61, 0x6e, 0x74,
	0x46, 0x69, 0x67, 0x75, 0x72, 0x65, 0x73, 0x1a, 0x24, 0x0a, 0x06, 0x42, 0x75, 0x63, 0x6b, 0x65,
	0x74, 0x12, 0x0c, 0x0a, 0x01, 0x76, 0x18, 0x01, 0x20, 0x01, 0x28, 0x03, 0x52, 0x01, 0x76, 0x12,
	0x0c, 0x0a, 0x01, 0x63, 0x18, 0x02, 0x20, 0x01, 0x28, 0x03, 0x52, 0x01, 0x63, 0x22, 0xe8, 0x03,
	0x0a, 0x0b, 0x53, 0x74, 0x61, 0x74, 0x73, 0x52, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x12, 0x14, 0x0a,
	0x05, 0x6c, 0x61, 0x62, 0x65, 0x6c, 0x18, 0x01, 0x20, 0x01, 0x28, 0x09, 0x52, 0x05, 0x6c, 0x61,
	0x62, 0x65, 0x6c, 0x12, 0x1c, 0x0a, 0x09, 0x74, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70,
	0x18, 0x02, 0x20, 0x01, 0x28, 0x09, 0x52, 0x09, 0x74, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d,
	0x70, 0x12, 0x17, 0x0a, 0x07, 0x64, 0x75, 0x72, 0x5f, 0x73, 0x65, 0x63, 0x18, 0x03, 0x20, 0x01,
	0x28, 0x01, 0x52, 0x06, 0x64, 0x75, 0x72, 0x53, 0x65, 0x63, 0x12, 0x20, 0x0a, 0x0c, 0x63, 0x75,
	0x6d, 0x5f, 0x6e, 0x75, 0x6d, 0x5f, 0x62, 0x69, 0x74, 0x73, 0x18, 0x0a, 0x20, 0x01, 0x28, 0x03,
	0x52, 0x0a, 0x63, 0x75, 0x6d, 0x4e, 0x75, 0x6d, 0x42, 0x69, 0x74, 0x73, 0x12, 0x20, 0x0a, 0x0c,
	0x63, 0x75, 0x6d, 0x5f, 0x6e, 0x75, 0x6d, 0x5f, 0x72, 0x70, 0x63, 0x73, 0x18, 0x0b, 0x20, 0x01,
	0x28, 0x03, 0x52, 0x0a, 0x63, 0x75, 0x6d, 0x4e, 0x75, 0x6d, 0x52, 0x70, 0x63, 0x73, 0x12, 0x29,
	0x0a, 0x11, 0x6d, 0x65, 0x61, 0x6e, 0x5f, 0x62, 0x69, 0x74, 0x73, 0x5f, 0x70, 0x65, 0x72, 0x5f,
	0x73, 0x65, 0x63, 0x18, 0x15, 0x20, 0x01, 0x28, 0x01, 0x52, 0x0e, 0x6d, 0x65, 0x61, 0x6e, 0x42,
	0x69, 0x74, 0x73, 0x50, 0x65, 0x72, 0x53, 0x65, 0x63, 0x12, 0x29, 0x0a, 0x11, 0x6d, 0x65, 0x61,
	0x6e, 0x5f, 0x72, 0x70, 0x63, 0x73, 0x5f, 0x70, 0x65, 0x72, 0x5f, 0x73, 0x65, 0x63, 0x18, 0x16,
	0x20, 0x01, 0x28, 0x01, 0x52, 0x0e, 0x6d, 0x65, 0x61, 0x6e, 0x52, 0x70, 0x63, 0x73, 0x50, 0x65,
	0x72, 0x53, 0x65, 0x63, 0x12, 0x3e, 0x0a, 0x07, 0x6c, 0x61, 0x74, 0x65, 0x6e, 0x63, 0x79, 0x18,
	0x17, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x24, 0x2e, 0x68, 0x65, 0x79, 0x70, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x2e, 0x53, 0x74, 0x61, 0x74, 0x73, 0x52, 0x65, 0x63, 0x6f, 0x72, 0x64, 0x2e, 0x4c,
	0x61, 0x74, 0x65, 0x6e, 0x63, 0x79, 0x53, 0x74, 0x61, 0x74, 0x73, 0x52, 0x07, 0x6c, 0x61, 0x74,
	0x65, 0x6e, 0x63, 0x79, 0x1a, 0xb1, 0x01, 0x0a, 0x0c, 0x4c, 0x61, 0x74, 0x65, 0x6e, 0x63, 0x79,
	0x53, 0x74, 0x61, 0x74, 0x73, 0x12, 0x12, 0x0a, 0x04, 0x6b, 0x69, 0x6e, 0x64, 0x18, 0x01, 0x20,
	0x01, 0x28, 0x09, 0x52, 0x04, 0x6b, 0x69, 0x6e, 0x64, 0x12, 0x31, 0x0a, 0x07, 0x68, 0x69, 0x73,
	0x74, 0x5f, 0x6e, 0x73, 0x18, 0x02, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x18, 0x2e, 0x68, 0x65, 0x79,
	0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x48, 0x64, 0x72, 0x48, 0x69, 0x73, 0x74, 0x6f,
	0x67, 0x72, 0x61, 0x6d, 0x52, 0x06, 0x68, 0x69, 0x73, 0x74, 0x4e, 0x73, 0x12, 0x15, 0x0a, 0x06,
	0x70, 0x35, 0x30, 0x5f, 0x6e, 0x73, 0x18, 0x0b, 0x20, 0x01, 0x28, 0x03, 0x52, 0x05, 0x70, 0x35,
	0x30, 0x4e, 0x73, 0x12, 0x15, 0x0a, 0x06, 0x70, 0x39, 0x30, 0x5f, 0x6e, 0x73, 0x18, 0x0c, 0x20,
	0x01, 0x28, 0x03, 0x52, 0x05, 0x70, 0x39, 0x30, 0x4e, 0x73, 0x12, 0x15, 0x0a, 0x06, 0x70, 0x39,
	0x35, 0x5f, 0x6e, 0x73, 0x18, 0x0d, 0x20, 0x01, 0x28, 0x03, 0x52, 0x05, 0x70, 0x39, 0x35, 0x4e,
	0x73, 0x12, 0x15, 0x0a, 0x06, 0x70, 0x39, 0x39, 0x5f, 0x6e, 0x73, 0x18, 0x0e, 0x20, 0x01, 0x28,
	0x03, 0x52, 0x05, 0x70, 0x39, 0x39, 0x4e, 0x73, 0x42, 0x25, 0x5a, 0x23, 0x67, 0x69, 0x74, 0x68,
	0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x75, 0x6c, 0x75, 0x79, 0x6f, 0x6c, 0x2f, 0x68, 0x65,
	0x79, 0x70, 0x2d, 0x61, 0x67, 0x65, 0x6e, 0x74, 0x73, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x62, 0x62,
	0x06, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x33,
}

var (
	file_heyp_proto_stats_proto_rawDescOnce sync.Once
	file_heyp_proto_stats_proto_rawDescData = file_heyp_proto_stats_proto_rawDesc
)

func file_heyp_proto_stats_proto_rawDescGZIP() []byte {
	file_heyp_proto_stats_proto_rawDescOnce.Do(func() {
		file_heyp_proto_stats_proto_rawDescData = protoimpl.X.CompressGZIP(file_heyp_proto_stats_proto_rawDescData)
	})
	return file_heyp_proto_stats_proto_rawDescData
}

var file_heyp_proto_stats_proto_msgTypes = make([]protoimpl.MessageInfo, 5)
var file_heyp_proto_stats_proto_goTypes = []interface{}{
	(*HdrHistogram)(nil),             // 0: heyp.proto.HdrHistogram
	(*StatsRecord)(nil),              // 1: heyp.proto.StatsRecord
	(*HdrHistogram_Config)(nil),      // 2: heyp.proto.HdrHistogram.Config
	(*HdrHistogram_Bucket)(nil),      // 3: heyp.proto.HdrHistogram.Bucket
	(*StatsRecord_LatencyStats)(nil), // 4: heyp.proto.StatsRecord.LatencyStats
}
var file_heyp_proto_stats_proto_depIdxs = []int32{
	2, // 0: heyp.proto.HdrHistogram.config:type_name -> heyp.proto.HdrHistogram.Config
	3, // 1: heyp.proto.HdrHistogram.buckets:type_name -> heyp.proto.HdrHistogram.Bucket
	4, // 2: heyp.proto.StatsRecord.latency:type_name -> heyp.proto.StatsRecord.LatencyStats
	0, // 3: heyp.proto.StatsRecord.LatencyStats.hist_ns:type_name -> heyp.proto.HdrHistogram
	4, // [4:4] is the sub-list for method output_type
	4, // [4:4] is the sub-list for method input_type
	4, // [4:4] is the sub-list for extension type_name
	4, // [4:4] is the sub-list for extension extendee
	0, // [0:4] is the sub-list for field type_name
}

func init() { file_heyp_proto_stats_proto_init() }
func file_heyp_proto_stats_proto_init() {
	if File_heyp_proto_stats_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_heyp_proto_stats_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*HdrHistogram); i {
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
		file_heyp_proto_stats_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*StatsRecord); i {
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
		file_heyp_proto_stats_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*HdrHistogram_Config); i {
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
		file_heyp_proto_stats_proto_msgTypes[3].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*HdrHistogram_Bucket); i {
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
		file_heyp_proto_stats_proto_msgTypes[4].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*StatsRecord_LatencyStats); i {
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
			RawDescriptor: file_heyp_proto_stats_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   5,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_heyp_proto_stats_proto_goTypes,
		DependencyIndexes: file_heyp_proto_stats_proto_depIdxs,
		MessageInfos:      file_heyp_proto_stats_proto_msgTypes,
	}.Build()
	File_heyp_proto_stats_proto = out.File
	file_heyp_proto_stats_proto_rawDesc = nil
	file_heyp_proto_stats_proto_goTypes = nil
	file_heyp_proto_stats_proto_depIdxs = nil
}