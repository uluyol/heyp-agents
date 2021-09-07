#include "heyp/proto/random.h"

#include <limits>
#include <string>
#include <string_view>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "heyp/log/spdlog.h"

namespace gpb = google::protobuf;

namespace heyp {

namespace {

class RandomFiller {
 public:
  void FillRandom(gpb::Message* mesg) {
    const gpb::Descriptor* d = mesg->GetDescriptor();
    const gpb::Reflection* r = mesg->GetReflection();

    for (int i = 0; i < d->field_count(); i++) {
      const gpb::FieldDescriptor* fd = d->field(i);

      if (fd->is_repeated()) {
        int len = absl::Uniform<int>(rng_, 0, 31);
        for (int k = 0; k < len; k++) {
          switch (fd->cpp_type()) {
            case gpb::FieldDescriptor::CPPTYPE_INT32:
              r->AddInt32(mesg, fd, RandomInt<int32_t>());
              break;
            case gpb::FieldDescriptor::CPPTYPE_INT64:
              r->AddInt64(mesg, fd, RandomInt<int64_t>());
              break;
            case gpb::FieldDescriptor::CPPTYPE_UINT32:
              r->AddUInt32(mesg, fd, RandomInt<uint32_t>());
              break;
            case gpb::FieldDescriptor::CPPTYPE_UINT64:
              r->AddUInt64(mesg, fd, RandomInt<uint64_t>());
              break;
            case gpb::FieldDescriptor::CPPTYPE_STRING:
              r->AddString(mesg, fd, RandomString());
              break;
            case gpb::FieldDescriptor::CPPTYPE_DOUBLE:
              r->AddDouble(mesg, fd, RandomFloat<double>());
              break;
            case gpb::FieldDescriptor::CPPTYPE_FLOAT:
              r->AddFloat(mesg, fd, RandomFloat<float>());
              break;
            case gpb::FieldDescriptor::CPPTYPE_BOOL:
              r->AddBool(mesg, fd, RandomBool());
              break;
            case gpb::FieldDescriptor::CPPTYPE_MESSAGE:
              FillRandom(r->AddMessage(mesg, fd));
              break;
            case gpb::FieldDescriptor::CPPTYPE_ENUM:
              r->AddEnumValue(mesg, fd, RandomEnum(fd->enum_type()));
              break;
            default:
              H_ASSERT_MESG(false, "unhandled FieldDescriptor type");
          }
        }
      } else {
        switch (fd->cpp_type()) {
          case gpb::FieldDescriptor::CPPTYPE_INT32:
            r->SetInt32(mesg, fd, RandomInt<int32_t>());
            break;
          case gpb::FieldDescriptor::CPPTYPE_INT64:
            r->SetInt64(mesg, fd, RandomInt<int64_t>());
            break;
          case gpb::FieldDescriptor::CPPTYPE_UINT32:
            r->SetUInt32(mesg, fd, RandomInt<uint32_t>());
            break;
          case gpb::FieldDescriptor::CPPTYPE_UINT64:
            r->SetUInt64(mesg, fd, RandomInt<uint64_t>());
            break;
          case gpb::FieldDescriptor::CPPTYPE_STRING:
            r->SetString(mesg, fd, RandomString());
            break;
          case gpb::FieldDescriptor::CPPTYPE_DOUBLE:
            r->SetDouble(mesg, fd, RandomFloat<double>());
            break;
          case gpb::FieldDescriptor::CPPTYPE_FLOAT:
            r->SetFloat(mesg, fd, RandomFloat<float>());
            break;
          case gpb::FieldDescriptor::CPPTYPE_BOOL:
            r->SetBool(mesg, fd, RandomBool());
            break;
          case gpb::FieldDescriptor::CPPTYPE_MESSAGE:
            FillRandom(r->MutableMessage(mesg, fd));
            break;
          case gpb::FieldDescriptor::CPPTYPE_ENUM:
            r->SetEnumValue(mesg, fd, RandomEnum(fd->enum_type()));
            break;
          default:
            H_ASSERT_MESG(false, "unhandled FieldDescriptor type");
        }
      }
    }
  }

  std::string RandomString() {
    constexpr std::string_view chars =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "1234567890"
        "!@#$%^&*()"
        "`~-_=+[{]}\\|;:'\",<.>/? ";

    int len = absl::Uniform<int>(rng_, 0, 31);

    std::string val(len, '\0');
    for (int i = 0; i < len; ++i) {
      val[i] = chars[absl::Uniform<int>(rng_, 0, chars.size())];
    }
    return val;
  }

  bool RandomBool() { return absl::Uniform<int>(rng_, 0, 2) == 0; }

  template <typename Int>
  Int RandomInt() {
    return absl::Uniform<Int>(rng_, std::numeric_limits<Int>::min(),
                              std::numeric_limits<Int>::max());
  }

  template <typename Float>
  Float RandomFloat() {
    return absl::Uniform<Float>(rng_, -9999999, 9999999);
  }

  int RandomEnum(const gpb::EnumDescriptor* d) {
    return absl::Uniform<int>(rng_, 0, d->value_count());
  }

 private:
  absl::BitGen rng_;
};

bool IsNonZero(const google::protobuf::Message& mesg) {
  const gpb::Descriptor* d = mesg.GetDescriptor();
  const gpb::Reflection* r = mesg.GetReflection();

  bool was_nonzero = false;
  for (int i = 0; i < d->field_count(); i++) {
    const gpb::FieldDescriptor* fd = d->field(i);

    switch (fd->cpp_type()) {
      case gpb::FieldDescriptor::CPPTYPE_INT32:
        was_nonzero = was_nonzero || r->GetInt32(mesg, fd) != 0;
        break;
      case gpb::FieldDescriptor::CPPTYPE_INT64:
        was_nonzero = was_nonzero || r->GetInt64(mesg, fd) != 0;
        break;
      case gpb::FieldDescriptor::CPPTYPE_UINT32:
        was_nonzero = was_nonzero || r->GetUInt32(mesg, fd) != 0;
        break;
      case gpb::FieldDescriptor::CPPTYPE_UINT64:
        was_nonzero = was_nonzero || r->GetUInt64(mesg, fd) != 0;
        break;
      case gpb::FieldDescriptor::CPPTYPE_STRING:
        was_nonzero = was_nonzero || r->GetString(mesg, fd) != "";
        break;
      case gpb::FieldDescriptor::CPPTYPE_DOUBLE:
        was_nonzero = was_nonzero || r->GetDouble(mesg, fd) != 0;
        break;
      case gpb::FieldDescriptor::CPPTYPE_FLOAT:
        was_nonzero = was_nonzero || r->GetFloat(mesg, fd) != 0;
        break;
      case gpb::FieldDescriptor::CPPTYPE_BOOL:
        was_nonzero = was_nonzero || r->GetBool(mesg, fd) != false;
        break;
      case gpb::FieldDescriptor::CPPTYPE_MESSAGE:
        was_nonzero = was_nonzero || IsNonZero(r->GetMessage(mesg, fd));
        break;
      case gpb::FieldDescriptor::CPPTYPE_ENUM:
        was_nonzero = was_nonzero || r->GetEnumValue(mesg, fd) != 0;
        break;
      default:
        H_ASSERT_MESG(false, "unhandled FieldDescriptor type");
    }
  }
  return was_nonzero;
}

}  // namespace

void FillRandomProto(gpb::Message* mesg) { RandomFiller().FillRandom(mesg); }

bool ClearRandomProtoField(google::protobuf::Message* mesg) {
  const gpb::Descriptor* d = mesg->GetDescriptor();
  const gpb::Reflection* r = mesg->GetReflection();

  const gpb::FieldDescriptor* fd =
      d->field(absl::Uniform(absl::BitGen(), 0, d->field_count()));

  bool was_nonzero = false;
  switch (fd->cpp_type()) {
    case gpb::FieldDescriptor::CPPTYPE_INT32:
      was_nonzero = r->GetInt32(*mesg, fd) != 0;
      break;
    case gpb::FieldDescriptor::CPPTYPE_INT64:
      was_nonzero = r->GetInt64(*mesg, fd) != 0;
      break;
    case gpb::FieldDescriptor::CPPTYPE_UINT32:
      was_nonzero = r->GetUInt32(*mesg, fd) != 0;
      break;
    case gpb::FieldDescriptor::CPPTYPE_UINT64:
      was_nonzero = r->GetUInt64(*mesg, fd) != 0;
      break;
    case gpb::FieldDescriptor::CPPTYPE_STRING:
      was_nonzero = r->GetString(*mesg, fd) != "";
      break;
    case gpb::FieldDescriptor::CPPTYPE_DOUBLE:
      was_nonzero = r->GetDouble(*mesg, fd) != 0;
      break;
    case gpb::FieldDescriptor::CPPTYPE_FLOAT:
      was_nonzero = r->GetFloat(*mesg, fd) != 0;
      break;
    case gpb::FieldDescriptor::CPPTYPE_BOOL:
      was_nonzero = r->GetBool(*mesg, fd) != false;
      break;
    case gpb::FieldDescriptor::CPPTYPE_MESSAGE:
      was_nonzero = IsNonZero(r->GetMessage(*mesg, fd));
      break;
    case gpb::FieldDescriptor::CPPTYPE_ENUM:
      was_nonzero = r->GetEnumValue(*mesg, fd) != 0;
      break;
    default:
      H_ASSERT_MESG(false, "unhandled FieldDescriptor type");
  }

  r->ClearField(mesg, fd);

  return was_nonzero;
}

}  // namespace heyp
