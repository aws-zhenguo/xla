#include "torch_xla/csrc/convert_ops.h"

#include <climits>

#include "torch_xla/csrc/aten_xla_bridge.h"
#include "torch_xla/csrc/dtype.h"
#include "torch_xla/csrc/helpers.h"
#include "torch_xla/csrc/runtime/debug_macros.h"
#include "torch_xla/csrc/tensor_util.h"
#include "xla/hlo/builder/lib/constants.h"
#include "xla/literal_util.h"
#include "xla/primitive_util.h"
#include "xla/shape_util.h"

namespace torch_xla {
namespace {

xla::XlaOp CreateRawMask(xla::XlaOp op, xla::PrimitiveType type, int64_t size,
                         int64_t narrow_size) {
  uint64_t mask_value =
      (static_cast<uint64_t>(1) << narrow_size * CHAR_BIT) - 1;
  xla::XlaOp mask = XlaHelpers::ScalarValue(mask_value, type, op.builder());
  if (xla::primitive_util::IsSignedIntegralType(type)) {
    // Sign extend the truncation mask.
    xla::XlaOp shift = XlaHelpers::ScalarValue<int32_t>(
        (size - narrow_size) * CHAR_BIT, op.builder());
    mask = (mask << shift) >> shift;
  }
  return mask;
}

xla::XlaOp ConvertData(xla::XlaOp op, xla::PrimitiveType type,
                       xla::PrimitiveType narrow_type) {
  if (!xla::primitive_util::IsIntegralType(type) ||
      !xla::primitive_util::IsIntegralType(narrow_type)) {
    return op;
  }
  int64_t size = xla::ShapeUtil::ByteSizeOfPrimitiveType(type);
  int64_t narrow_size = xla::ShapeUtil::ByteSizeOfPrimitiveType(narrow_type);
  XLA_CHECK_GE(size, narrow_size);
  if (size == narrow_size) {
    return op;
  }
  xla::XlaOp mask = CreateRawMask(op, type, size, narrow_size);
  return op & mask;
}

}  // namespace

xla::XlaOp ConvertTo(xla::XlaOp op, xla::PrimitiveType from,
                     xla::PrimitiveType to) {
  if (from == to) {
    return op;
  }
  return xla::ConvertElementType(op, to);
}

xla::XlaOp ConvertToRaw(xla::XlaOp op, xla::PrimitiveType from,
                        xla::PrimitiveType raw_from, xla::PrimitiveType to,
                        xla::PrimitiveType raw_to) {
  if (from != raw_from) {
    op = ConvertData(op, from, raw_from);
  }
  xla::XlaOp result = ConvertTo(op, from, to);
  return to == raw_to ? result : ConvertData(result, to, raw_to);
}

xla::XlaOp ConvertToNumeric(xla::XlaOp op, xla::PrimitiveType from) {
  if (from == xla::PrimitiveType::PRED) {
    torch::lazy::BackendDevice xla_device = bridge::GetCurrentDevice();
    op = ConvertTo(
        op, from,
        MaybeDowncastToXlaDeviceType(xla::PrimitiveType::U8, xla_device));
  }
  return op;
}

xla::XlaOp ConvertToNumeric(xla::XlaOp op) {
  return ConvertToNumeric(op, XlaHelpers::TypeOfXlaOp(op));
}

xla::XlaOp CastToScalarType(xla::XlaOp input,
                            std::optional<at::ScalarType> dtype) {
  if (dtype) {
    torch::lazy::BackendDevice xla_device = bridge::GetCurrentDevice();
    return ConvertTo(input, XlaHelpers::TypeOfXlaOp(input),
                     MakeXlaPrimitiveType(*dtype, &xla_device));
  }
  return ConvertToNumeric(input, XlaHelpers::TypeOfXlaOp(input));
}

xla::XlaOp MaybeConvertTo(xla::XlaOp input, xla::PrimitiveType type) {
  return XlaHelpers::TypeOfXlaOp(input) != type
             ? xla::ConvertElementType(input, type)
             : input;
}

}  // namespace torch_xla
