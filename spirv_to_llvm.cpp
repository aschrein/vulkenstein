// #include <llvm/ExecutionEngine/ExecutionEngine.h>
// #include <llvm/ExecutionEngine/Orc/CompileUtils.h>
// #include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
// #include <llvm/ExecutionEngine/Orc/LambdaResolver.h>
// #include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
// #include <llvm/ExecutionEngine/RTDyldMemoryManager.h>
// #include <llvm/ExecutionEngine/SectionMemoryManager.h>
// #include <llvm/IR/DerivedTypes.h>
// #include <llvm/IR/IRBuilder.h>
// #include <llvm/IR/LegacyPassManager.h>
// #include <llvm/IR/Mangler.h>
// #include <llvm/IR/Module.h>
// #include <llvm/IR/Verifier.h>
// #include <llvm/Support/DynamicLibrary.h>
// #include <llvm/Support/TargetSelect.h>
// #include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include "3rdparty/SPIRV/spirv.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <set>

#define UTILS_IMPL
#include "utils.hpp"

// #include "3rdparty/spirv_cross/spirv_parser.hpp"
// #include "3rdparty/spirv_cross/spirv_cpp.hpp"
// #include "3rdparty/spirv_cross/spirv_ispc.hpp"
// #include "3rdparty/spirv_cross/spirv_llvm.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <stdio.h>

#include "llvm_stdlib.h"

#define UNIMPLEMENTED_(s)                                                      \
  {                                                                            \
    fprintf(stderr, "UNIMPLEMENTED %s: %s:%i\n", s, __FILE__, __LINE__);       \
    exit(1);                                                                   \
  }

#define UNIMPLEMENTED UNIMPLEMENTED_("")

#define LOOKUP_DECL(name) module->getFunction((name))
#define LOOKUP_TY(name) module->getTypeByName((name))

void llvm_fatal(void *user_data, const std::string &reason,
                bool gen_crash_diag) {
  fprintf(stderr, "[LLVM_FATAL] %s\n", reason.c_str());
  (void)(*(int *)(void *)(0) = 0);
}

#ifdef S2L_EXE

std::vector<uint8_t> readFile(const char *filename) {
  // open the file:
  std::ifstream file(filename, std::ios::binary);

  // Stop eating new lines in binary mode!!!
  file.unsetf(std::ios::skipws);

  // get its size:
  std::streampos fileSize;

  file.seekg(0, std::ios::end);
  fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  // reserve capacity
  std::vector<uint8_t> vec;
  vec.reserve(fileSize);

  // read the data:
  vec.insert(vec.begin(), std::istream_iterator<uint8_t>(file),
             std::istream_iterator<uint8_t>());

  return vec;
}
//////////////////////////
// Meta data structures //
//////////////////////////
struct FunTy {
  uint32_t id;
  std::vector<uint32_t> params;
  uint32_t ret;
};
struct ImageTy {
  uint32_t id;
  uint32_t sampled_type;
  spv::Dim dim;
  bool depth;
  bool arrayed;
  bool ms;
  uint32_t sampled;
  spv::ImageFormat format;
  spv::AccessQualifier access;
};
struct Sampled_ImageTy {
  uint32_t id;
  uint32_t sampled_image;
};
struct SamplerTy {
  uint32_t id;
};
struct Decoration {
  uint32_t target_id;
  spv::Decoration type;
  uint32_t param1;
  uint32_t param2;
  uint32_t param3;
  uint32_t param4;
};
struct Member_Decoration {
  uint32_t target_id;
  uint32_t member_id;
  spv::Decoration type;
  uint32_t param1;
  uint32_t param2;
  uint32_t param3;
  uint32_t param4;
};
enum class Primitive_t {
  I1,
  I8,
  I16,
  I32,
  I64,
  U8,
  U16,
  U32,
  U64,
  F8,
  F16,
  F32,
  F64,
  Void
};
struct PrimitiveTy {
  uint32_t id;
  Primitive_t type;
};
struct VectorTy {
  uint32_t id;
  // Must be primitive?
  uint32_t member_id;
  // the number of rows
  uint32_t width;
};
struct Constant {
  uint32_t id;
  uint32_t type;
  union {
    uint32_t i32_val;
    float f32_val;
  };
};
struct ArrayTy {
  uint32_t id;
  // could be anything?
  uint32_t member_id;
  // constants[width_id]
  uint32_t width_id;
};
struct MatrixTy {
  uint32_t id;
  // vector_types[vector_id] : column type
  uint32_t vector_id;
  // the number of columns
  uint32_t width;
};
struct StructTy {
  uint32_t id;
  std::vector<uint32_t> member_types;
};
struct PtrTy {
  uint32_t id;
  uint32_t target_id;
  spv::StorageClass storage_class;
};
struct Variable {
  uint32_t id;
  // Must be a pointer
  uint32_t type_id;
  spv::StorageClass storage;
  // Optional
  uint32_t init_id;
};
struct Function {
  uint32_t id;
  uint32_t result_type;
  spv::FunctionControlMask control;
  uint32_t function_type;
};
enum class DeclTy {
  PrimitiveTy,
  Variable,
  Function,
  PtrTy,
  VectorTy,
  Constant,
  ArrayTy,
  ImageTy,
  SamplerTy,
  Sampled_ImageTy,
  FunTy,
  MatrixTy,
  StructTy,
  Unknown
};
void dump(char const *cstr) { fprintf(stdout, "%s", cstr); }
void dump(uint32_t num) { fprintf(stdout, "%i", num); }
#if 1 // AUTOGENERATED
#if 1 // AUTOGENERATED
char const *get_cstr(spv::SourceLanguage const &str) {
  switch (str) {
  case spv::SourceLanguage::SourceLanguageUnknown:
    return "SourceLanguageUnknown";
  case spv::SourceLanguage::SourceLanguageESSL:
    return "SourceLanguageESSL";
  case spv::SourceLanguage::SourceLanguageGLSL:
    return "SourceLanguageGLSL";
  case spv::SourceLanguage::SourceLanguageOpenCL_C:
    return "SourceLanguageOpenCL_C";
  case spv::SourceLanguage::SourceLanguageOpenCL_CPP:
    return "SourceLanguageOpenCL_CPP";
  case spv::SourceLanguage::SourceLanguageHLSL:
    return "SourceLanguageHLSL";
  case spv::SourceLanguage::SourceLanguageMax:
    return "SourceLanguageMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::SourceLanguage const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::ExecutionModel const &str) {
  switch (str) {
  case spv::ExecutionModel::ExecutionModelVertex:
    return "ExecutionModelVertex";
  case spv::ExecutionModel::ExecutionModelTessellationControl:
    return "ExecutionModelTessellationControl";
  case spv::ExecutionModel::ExecutionModelTessellationEvaluation:
    return "ExecutionModelTessellationEvaluation";
  case spv::ExecutionModel::ExecutionModelGeometry:
    return "ExecutionModelGeometry";
  case spv::ExecutionModel::ExecutionModelFragment:
    return "ExecutionModelFragment";
  case spv::ExecutionModel::ExecutionModelGLCompute:
    return "ExecutionModelGLCompute";
  case spv::ExecutionModel::ExecutionModelKernel:
    return "ExecutionModelKernel";
  case spv::ExecutionModel::ExecutionModelTaskNV:
    return "ExecutionModelTaskNV";
  case spv::ExecutionModel::ExecutionModelMeshNV:
    return "ExecutionModelMeshNV";
  case spv::ExecutionModel::ExecutionModelRayGenerationNV:
    return "ExecutionModelRayGenerationNV";
  case spv::ExecutionModel::ExecutionModelIntersectionNV:
    return "ExecutionModelIntersectionNV";
  case spv::ExecutionModel::ExecutionModelAnyHitNV:
    return "ExecutionModelAnyHitNV";
  case spv::ExecutionModel::ExecutionModelClosestHitNV:
    return "ExecutionModelClosestHitNV";
  case spv::ExecutionModel::ExecutionModelMissNV:
    return "ExecutionModelMissNV";
  case spv::ExecutionModel::ExecutionModelCallableNV:
    return "ExecutionModelCallableNV";
  case spv::ExecutionModel::ExecutionModelMax:
    return "ExecutionModelMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::ExecutionModel const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::AddressingModel const &str) {
  switch (str) {
  case spv::AddressingModel::AddressingModelLogical:
    return "AddressingModelLogical";
  case spv::AddressingModel::AddressingModelPhysical32:
    return "AddressingModelPhysical32";
  case spv::AddressingModel::AddressingModelPhysical64:
    return "AddressingModelPhysical64";
  case spv::AddressingModel::AddressingModelPhysicalStorageBuffer64:
    return "AddressingModelPhysicalStorageBuffer64";
    //    case spv::AddressingModel::AddressingModelPhysicalStorageBuffer64EXT :
    //    return "AddressingModelPhysicalStorageBuffer64EXT";
  case spv::AddressingModel::AddressingModelMax:
    return "AddressingModelMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::AddressingModel const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::MemoryModel const &str) {
  switch (str) {
  case spv::MemoryModel::MemoryModelSimple:
    return "MemoryModelSimple";
  case spv::MemoryModel::MemoryModelGLSL450:
    return "MemoryModelGLSL450";
  case spv::MemoryModel::MemoryModelOpenCL:
    return "MemoryModelOpenCL";
  case spv::MemoryModel::MemoryModelVulkan:
    return "MemoryModelVulkan";
    //    case spv::MemoryModel::MemoryModelVulkanKHR : return
    //    "MemoryModelVulkanKHR";
  case spv::MemoryModel::MemoryModelMax:
    return "MemoryModelMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::MemoryModel const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::ExecutionMode const &str) {
  switch (str) {
  case spv::ExecutionMode::ExecutionModeInvocations:
    return "ExecutionModeInvocations";
  case spv::ExecutionMode::ExecutionModeSpacingEqual:
    return "ExecutionModeSpacingEqual";
  case spv::ExecutionMode::ExecutionModeSpacingFractionalEven:
    return "ExecutionModeSpacingFractionalEven";
  case spv::ExecutionMode::ExecutionModeSpacingFractionalOdd:
    return "ExecutionModeSpacingFractionalOdd";
  case spv::ExecutionMode::ExecutionModeVertexOrderCw:
    return "ExecutionModeVertexOrderCw";
  case spv::ExecutionMode::ExecutionModeVertexOrderCcw:
    return "ExecutionModeVertexOrderCcw";
  case spv::ExecutionMode::ExecutionModePixelCenterInteger:
    return "ExecutionModePixelCenterInteger";
  case spv::ExecutionMode::ExecutionModeOriginUpperLeft:
    return "ExecutionModeOriginUpperLeft";
  case spv::ExecutionMode::ExecutionModeOriginLowerLeft:
    return "ExecutionModeOriginLowerLeft";
  case spv::ExecutionMode::ExecutionModeEarlyFragmentTests:
    return "ExecutionModeEarlyFragmentTests";
  case spv::ExecutionMode::ExecutionModePointMode:
    return "ExecutionModePointMode";
  case spv::ExecutionMode::ExecutionModeXfb:
    return "ExecutionModeXfb";
  case spv::ExecutionMode::ExecutionModeDepthReplacing:
    return "ExecutionModeDepthReplacing";
  case spv::ExecutionMode::ExecutionModeDepthGreater:
    return "ExecutionModeDepthGreater";
  case spv::ExecutionMode::ExecutionModeDepthLess:
    return "ExecutionModeDepthLess";
  case spv::ExecutionMode::ExecutionModeDepthUnchanged:
    return "ExecutionModeDepthUnchanged";
  case spv::ExecutionMode::ExecutionModeLocalSize:
    return "ExecutionModeLocalSize";
  case spv::ExecutionMode::ExecutionModeLocalSizeHint:
    return "ExecutionModeLocalSizeHint";
  case spv::ExecutionMode::ExecutionModeInputPoints:
    return "ExecutionModeInputPoints";
  case spv::ExecutionMode::ExecutionModeInputLines:
    return "ExecutionModeInputLines";
  case spv::ExecutionMode::ExecutionModeInputLinesAdjacency:
    return "ExecutionModeInputLinesAdjacency";
  case spv::ExecutionMode::ExecutionModeTriangles:
    return "ExecutionModeTriangles";
  case spv::ExecutionMode::ExecutionModeInputTrianglesAdjacency:
    return "ExecutionModeInputTrianglesAdjacency";
  case spv::ExecutionMode::ExecutionModeQuads:
    return "ExecutionModeQuads";
  case spv::ExecutionMode::ExecutionModeIsolines:
    return "ExecutionModeIsolines";
  case spv::ExecutionMode::ExecutionModeOutputVertices:
    return "ExecutionModeOutputVertices";
  case spv::ExecutionMode::ExecutionModeOutputPoints:
    return "ExecutionModeOutputPoints";
  case spv::ExecutionMode::ExecutionModeOutputLineStrip:
    return "ExecutionModeOutputLineStrip";
  case spv::ExecutionMode::ExecutionModeOutputTriangleStrip:
    return "ExecutionModeOutputTriangleStrip";
  case spv::ExecutionMode::ExecutionModeVecTypeHint:
    return "ExecutionModeVecTypeHint";
  case spv::ExecutionMode::ExecutionModeContractionOff:
    return "ExecutionModeContractionOff";
  case spv::ExecutionMode::ExecutionModeInitializer:
    return "ExecutionModeInitializer";
  case spv::ExecutionMode::ExecutionModeFinalizer:
    return "ExecutionModeFinalizer";
  case spv::ExecutionMode::ExecutionModeSubgroupSize:
    return "ExecutionModeSubgroupSize";
  case spv::ExecutionMode::ExecutionModeSubgroupsPerWorkgroup:
    return "ExecutionModeSubgroupsPerWorkgroup";
  case spv::ExecutionMode::ExecutionModeSubgroupsPerWorkgroupId:
    return "ExecutionModeSubgroupsPerWorkgroupId";
  case spv::ExecutionMode::ExecutionModeLocalSizeId:
    return "ExecutionModeLocalSizeId";
  case spv::ExecutionMode::ExecutionModeLocalSizeHintId:
    return "ExecutionModeLocalSizeHintId";
  case spv::ExecutionMode::ExecutionModePostDepthCoverage:
    return "ExecutionModePostDepthCoverage";
  case spv::ExecutionMode::ExecutionModeDenormPreserve:
    return "ExecutionModeDenormPreserve";
  case spv::ExecutionMode::ExecutionModeDenormFlushToZero:
    return "ExecutionModeDenormFlushToZero";
  case spv::ExecutionMode::ExecutionModeSignedZeroInfNanPreserve:
    return "ExecutionModeSignedZeroInfNanPreserve";
  case spv::ExecutionMode::ExecutionModeRoundingModeRTE:
    return "ExecutionModeRoundingModeRTE";
  case spv::ExecutionMode::ExecutionModeRoundingModeRTZ:
    return "ExecutionModeRoundingModeRTZ";
  case spv::ExecutionMode::ExecutionModeStencilRefReplacingEXT:
    return "ExecutionModeStencilRefReplacingEXT";
  case spv::ExecutionMode::ExecutionModeOutputLinesNV:
    return "ExecutionModeOutputLinesNV";
  case spv::ExecutionMode::ExecutionModeOutputPrimitivesNV:
    return "ExecutionModeOutputPrimitivesNV";
  case spv::ExecutionMode::ExecutionModeDerivativeGroupQuadsNV:
    return "ExecutionModeDerivativeGroupQuadsNV";
  case spv::ExecutionMode::ExecutionModeDerivativeGroupLinearNV:
    return "ExecutionModeDerivativeGroupLinearNV";
  case spv::ExecutionMode::ExecutionModeOutputTrianglesNV:
    return "ExecutionModeOutputTrianglesNV";
  case spv::ExecutionMode::ExecutionModePixelInterlockOrderedEXT:
    return "ExecutionModePixelInterlockOrderedEXT";
  case spv::ExecutionMode::ExecutionModePixelInterlockUnorderedEXT:
    return "ExecutionModePixelInterlockUnorderedEXT";
  case spv::ExecutionMode::ExecutionModeSampleInterlockOrderedEXT:
    return "ExecutionModeSampleInterlockOrderedEXT";
  case spv::ExecutionMode::ExecutionModeSampleInterlockUnorderedEXT:
    return "ExecutionModeSampleInterlockUnorderedEXT";
  case spv::ExecutionMode::ExecutionModeShadingRateInterlockOrderedEXT:
    return "ExecutionModeShadingRateInterlockOrderedEXT";
  case spv::ExecutionMode::ExecutionModeShadingRateInterlockUnorderedEXT:
    return "ExecutionModeShadingRateInterlockUnorderedEXT";
  case spv::ExecutionMode::ExecutionModeMax:
    return "ExecutionModeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::ExecutionMode const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::StorageClass const &str) {
  switch (str) {
  case spv::StorageClass::StorageClassUniformConstant:
    return "StorageClassUniformConstant";
  case spv::StorageClass::StorageClassInput:
    return "StorageClassInput";
  case spv::StorageClass::StorageClassUniform:
    return "StorageClassUniform";
  case spv::StorageClass::StorageClassOutput:
    return "StorageClassOutput";
  case spv::StorageClass::StorageClassWorkgroup:
    return "StorageClassWorkgroup";
  case spv::StorageClass::StorageClassCrossWorkgroup:
    return "StorageClassCrossWorkgroup";
  case spv::StorageClass::StorageClassPrivate:
    return "StorageClassPrivate";
  case spv::StorageClass::StorageClassFunction:
    return "StorageClassFunction";
  case spv::StorageClass::StorageClassGeneric:
    return "StorageClassGeneric";
  case spv::StorageClass::StorageClassPushConstant:
    return "StorageClassPushConstant";
  case spv::StorageClass::StorageClassAtomicCounter:
    return "StorageClassAtomicCounter";
  case spv::StorageClass::StorageClassImage:
    return "StorageClassImage";
  case spv::StorageClass::StorageClassStorageBuffer:
    return "StorageClassStorageBuffer";
  case spv::StorageClass::StorageClassCallableDataNV:
    return "StorageClassCallableDataNV";
  case spv::StorageClass::StorageClassIncomingCallableDataNV:
    return "StorageClassIncomingCallableDataNV";
  case spv::StorageClass::StorageClassRayPayloadNV:
    return "StorageClassRayPayloadNV";
  case spv::StorageClass::StorageClassHitAttributeNV:
    return "StorageClassHitAttributeNV";
  case spv::StorageClass::StorageClassIncomingRayPayloadNV:
    return "StorageClassIncomingRayPayloadNV";
  case spv::StorageClass::StorageClassShaderRecordBufferNV:
    return "StorageClassShaderRecordBufferNV";
  case spv::StorageClass::StorageClassPhysicalStorageBuffer:
    return "StorageClassPhysicalStorageBuffer";
    //    case spv::StorageClass::StorageClassPhysicalStorageBufferEXT : return
    //    "StorageClassPhysicalStorageBufferEXT";
  case spv::StorageClass::StorageClassMax:
    return "StorageClassMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::StorageClass const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::Dim const &str) {
  switch (str) {
  case spv::Dim::Dim1D:
    return "Dim1D";
  case spv::Dim::Dim2D:
    return "Dim2D";
  case spv::Dim::Dim3D:
    return "Dim3D";
  case spv::Dim::DimCube:
    return "DimCube";
  case spv::Dim::DimRect:
    return "DimRect";
  case spv::Dim::DimBuffer:
    return "DimBuffer";
  case spv::Dim::DimSubpassData:
    return "DimSubpassData";
  case spv::Dim::DimMax:
    return "DimMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::Dim const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::SamplerAddressingMode const &str) {
  switch (str) {
  case spv::SamplerAddressingMode::SamplerAddressingModeNone:
    return "SamplerAddressingModeNone";
  case spv::SamplerAddressingMode::SamplerAddressingModeClampToEdge:
    return "SamplerAddressingModeClampToEdge";
  case spv::SamplerAddressingMode::SamplerAddressingModeClamp:
    return "SamplerAddressingModeClamp";
  case spv::SamplerAddressingMode::SamplerAddressingModeRepeat:
    return "SamplerAddressingModeRepeat";
  case spv::SamplerAddressingMode::SamplerAddressingModeRepeatMirrored:
    return "SamplerAddressingModeRepeatMirrored";
  case spv::SamplerAddressingMode::SamplerAddressingModeMax:
    return "SamplerAddressingModeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::SamplerAddressingMode const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::SamplerFilterMode const &str) {
  switch (str) {
  case spv::SamplerFilterMode::SamplerFilterModeNearest:
    return "SamplerFilterModeNearest";
  case spv::SamplerFilterMode::SamplerFilterModeLinear:
    return "SamplerFilterModeLinear";
  case spv::SamplerFilterMode::SamplerFilterModeMax:
    return "SamplerFilterModeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::SamplerFilterMode const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::ImageFormat const &str) {
  switch (str) {
  case spv::ImageFormat::ImageFormatUnknown:
    return "ImageFormatUnknown";
  case spv::ImageFormat::ImageFormatRgba32f:
    return "ImageFormatRgba32f";
  case spv::ImageFormat::ImageFormatRgba16f:
    return "ImageFormatRgba16f";
  case spv::ImageFormat::ImageFormatR32f:
    return "ImageFormatR32f";
  case spv::ImageFormat::ImageFormatRgba8:
    return "ImageFormatRgba8";
  case spv::ImageFormat::ImageFormatRgba8Snorm:
    return "ImageFormatRgba8Snorm";
  case spv::ImageFormat::ImageFormatRg32f:
    return "ImageFormatRg32f";
  case spv::ImageFormat::ImageFormatRg16f:
    return "ImageFormatRg16f";
  case spv::ImageFormat::ImageFormatR11fG11fB10f:
    return "ImageFormatR11fG11fB10f";
  case spv::ImageFormat::ImageFormatR16f:
    return "ImageFormatR16f";
  case spv::ImageFormat::ImageFormatRgba16:
    return "ImageFormatRgba16";
  case spv::ImageFormat::ImageFormatRgb10A2:
    return "ImageFormatRgb10A2";
  case spv::ImageFormat::ImageFormatRg16:
    return "ImageFormatRg16";
  case spv::ImageFormat::ImageFormatRg8:
    return "ImageFormatRg8";
  case spv::ImageFormat::ImageFormatR16:
    return "ImageFormatR16";
  case spv::ImageFormat::ImageFormatR8:
    return "ImageFormatR8";
  case spv::ImageFormat::ImageFormatRgba16Snorm:
    return "ImageFormatRgba16Snorm";
  case spv::ImageFormat::ImageFormatRg16Snorm:
    return "ImageFormatRg16Snorm";
  case spv::ImageFormat::ImageFormatRg8Snorm:
    return "ImageFormatRg8Snorm";
  case spv::ImageFormat::ImageFormatR16Snorm:
    return "ImageFormatR16Snorm";
  case spv::ImageFormat::ImageFormatR8Snorm:
    return "ImageFormatR8Snorm";
  case spv::ImageFormat::ImageFormatRgba32i:
    return "ImageFormatRgba32i";
  case spv::ImageFormat::ImageFormatRgba16i:
    return "ImageFormatRgba16i";
  case spv::ImageFormat::ImageFormatRgba8i:
    return "ImageFormatRgba8i";
  case spv::ImageFormat::ImageFormatR32i:
    return "ImageFormatR32i";
  case spv::ImageFormat::ImageFormatRg32i:
    return "ImageFormatRg32i";
  case spv::ImageFormat::ImageFormatRg16i:
    return "ImageFormatRg16i";
  case spv::ImageFormat::ImageFormatRg8i:
    return "ImageFormatRg8i";
  case spv::ImageFormat::ImageFormatR16i:
    return "ImageFormatR16i";
  case spv::ImageFormat::ImageFormatR8i:
    return "ImageFormatR8i";
  case spv::ImageFormat::ImageFormatRgba32ui:
    return "ImageFormatRgba32ui";
  case spv::ImageFormat::ImageFormatRgba16ui:
    return "ImageFormatRgba16ui";
  case spv::ImageFormat::ImageFormatRgba8ui:
    return "ImageFormatRgba8ui";
  case spv::ImageFormat::ImageFormatR32ui:
    return "ImageFormatR32ui";
  case spv::ImageFormat::ImageFormatRgb10a2ui:
    return "ImageFormatRgb10a2ui";
  case spv::ImageFormat::ImageFormatRg32ui:
    return "ImageFormatRg32ui";
  case spv::ImageFormat::ImageFormatRg16ui:
    return "ImageFormatRg16ui";
  case spv::ImageFormat::ImageFormatRg8ui:
    return "ImageFormatRg8ui";
  case spv::ImageFormat::ImageFormatR16ui:
    return "ImageFormatR16ui";
  case spv::ImageFormat::ImageFormatR8ui:
    return "ImageFormatR8ui";
  case spv::ImageFormat::ImageFormatMax:
    return "ImageFormatMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::ImageFormat const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::ImageChannelOrder const &str) {
  switch (str) {
  case spv::ImageChannelOrder::ImageChannelOrderR:
    return "ImageChannelOrderR";
  case spv::ImageChannelOrder::ImageChannelOrderA:
    return "ImageChannelOrderA";
  case spv::ImageChannelOrder::ImageChannelOrderRG:
    return "ImageChannelOrderRG";
  case spv::ImageChannelOrder::ImageChannelOrderRA:
    return "ImageChannelOrderRA";
  case spv::ImageChannelOrder::ImageChannelOrderRGB:
    return "ImageChannelOrderRGB";
  case spv::ImageChannelOrder::ImageChannelOrderRGBA:
    return "ImageChannelOrderRGBA";
  case spv::ImageChannelOrder::ImageChannelOrderBGRA:
    return "ImageChannelOrderBGRA";
  case spv::ImageChannelOrder::ImageChannelOrderARGB:
    return "ImageChannelOrderARGB";
  case spv::ImageChannelOrder::ImageChannelOrderIntensity:
    return "ImageChannelOrderIntensity";
  case spv::ImageChannelOrder::ImageChannelOrderLuminance:
    return "ImageChannelOrderLuminance";
  case spv::ImageChannelOrder::ImageChannelOrderRx:
    return "ImageChannelOrderRx";
  case spv::ImageChannelOrder::ImageChannelOrderRGx:
    return "ImageChannelOrderRGx";
  case spv::ImageChannelOrder::ImageChannelOrderRGBx:
    return "ImageChannelOrderRGBx";
  case spv::ImageChannelOrder::ImageChannelOrderDepth:
    return "ImageChannelOrderDepth";
  case spv::ImageChannelOrder::ImageChannelOrderDepthStencil:
    return "ImageChannelOrderDepthStencil";
  case spv::ImageChannelOrder::ImageChannelOrdersRGB:
    return "ImageChannelOrdersRGB";
  case spv::ImageChannelOrder::ImageChannelOrdersRGBx:
    return "ImageChannelOrdersRGBx";
  case spv::ImageChannelOrder::ImageChannelOrdersRGBA:
    return "ImageChannelOrdersRGBA";
  case spv::ImageChannelOrder::ImageChannelOrdersBGRA:
    return "ImageChannelOrdersBGRA";
  case spv::ImageChannelOrder::ImageChannelOrderABGR:
    return "ImageChannelOrderABGR";
  case spv::ImageChannelOrder::ImageChannelOrderMax:
    return "ImageChannelOrderMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::ImageChannelOrder const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::ImageChannelDataType const &str) {
  switch (str) {
  case spv::ImageChannelDataType::ImageChannelDataTypeSnormInt8:
    return "ImageChannelDataTypeSnormInt8";
  case spv::ImageChannelDataType::ImageChannelDataTypeSnormInt16:
    return "ImageChannelDataTypeSnormInt16";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnormInt8:
    return "ImageChannelDataTypeUnormInt8";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnormInt16:
    return "ImageChannelDataTypeUnormInt16";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnormShort565:
    return "ImageChannelDataTypeUnormShort565";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnormShort555:
    return "ImageChannelDataTypeUnormShort555";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnormInt101010:
    return "ImageChannelDataTypeUnormInt101010";
  case spv::ImageChannelDataType::ImageChannelDataTypeSignedInt8:
    return "ImageChannelDataTypeSignedInt8";
  case spv::ImageChannelDataType::ImageChannelDataTypeSignedInt16:
    return "ImageChannelDataTypeSignedInt16";
  case spv::ImageChannelDataType::ImageChannelDataTypeSignedInt32:
    return "ImageChannelDataTypeSignedInt32";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnsignedInt8:
    return "ImageChannelDataTypeUnsignedInt8";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnsignedInt16:
    return "ImageChannelDataTypeUnsignedInt16";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnsignedInt32:
    return "ImageChannelDataTypeUnsignedInt32";
  case spv::ImageChannelDataType::ImageChannelDataTypeHalfFloat:
    return "ImageChannelDataTypeHalfFloat";
  case spv::ImageChannelDataType::ImageChannelDataTypeFloat:
    return "ImageChannelDataTypeFloat";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnormInt24:
    return "ImageChannelDataTypeUnormInt24";
  case spv::ImageChannelDataType::ImageChannelDataTypeUnormInt101010_2:
    return "ImageChannelDataTypeUnormInt101010_2";
  case spv::ImageChannelDataType::ImageChannelDataTypeMax:
    return "ImageChannelDataTypeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::ImageChannelDataType const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::ImageOperandsShift const &str) {
  switch (str) {
  case spv::ImageOperandsShift::ImageOperandsBiasShift:
    return "ImageOperandsBiasShift";
  case spv::ImageOperandsShift::ImageOperandsLodShift:
    return "ImageOperandsLodShift";
  case spv::ImageOperandsShift::ImageOperandsGradShift:
    return "ImageOperandsGradShift";
  case spv::ImageOperandsShift::ImageOperandsConstOffsetShift:
    return "ImageOperandsConstOffsetShift";
  case spv::ImageOperandsShift::ImageOperandsOffsetShift:
    return "ImageOperandsOffsetShift";
  case spv::ImageOperandsShift::ImageOperandsConstOffsetsShift:
    return "ImageOperandsConstOffsetsShift";
  case spv::ImageOperandsShift::ImageOperandsSampleShift:
    return "ImageOperandsSampleShift";
  case spv::ImageOperandsShift::ImageOperandsMinLodShift:
    return "ImageOperandsMinLodShift";
  case spv::ImageOperandsShift::ImageOperandsMakeTexelAvailableShift:
    return "ImageOperandsMakeTexelAvailableShift";
    //    case spv::ImageOperandsShift::ImageOperandsMakeTexelAvailableKHRShift
    //    : return "ImageOperandsMakeTexelAvailableKHRShift";
  case spv::ImageOperandsShift::ImageOperandsMakeTexelVisibleShift:
    return "ImageOperandsMakeTexelVisibleShift";
    //    case spv::ImageOperandsShift::ImageOperandsMakeTexelVisibleKHRShift :
    //    return "ImageOperandsMakeTexelVisibleKHRShift";
  case spv::ImageOperandsShift::ImageOperandsNonPrivateTexelShift:
    return "ImageOperandsNonPrivateTexelShift";
    //    case spv::ImageOperandsShift::ImageOperandsNonPrivateTexelKHRShift :
    //    return "ImageOperandsNonPrivateTexelKHRShift";
  case spv::ImageOperandsShift::ImageOperandsVolatileTexelShift:
    return "ImageOperandsVolatileTexelShift";
    //    case spv::ImageOperandsShift::ImageOperandsVolatileTexelKHRShift :
    //    return "ImageOperandsVolatileTexelKHRShift";
  case spv::ImageOperandsShift::ImageOperandsSignExtendShift:
    return "ImageOperandsSignExtendShift";
  case spv::ImageOperandsShift::ImageOperandsZeroExtendShift:
    return "ImageOperandsZeroExtendShift";
  case spv::ImageOperandsShift::ImageOperandsMax:
    return "ImageOperandsMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::ImageOperandsShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::ImageOperandsMask const &str) {
  switch (str) {
  case spv::ImageOperandsMask::ImageOperandsMaskNone:
    return "ImageOperandsMaskNone";
  case spv::ImageOperandsMask::ImageOperandsBiasMask:
    return "ImageOperandsBiasMask";
  case spv::ImageOperandsMask::ImageOperandsLodMask:
    return "ImageOperandsLodMask";
  case spv::ImageOperandsMask::ImageOperandsGradMask:
    return "ImageOperandsGradMask";
  case spv::ImageOperandsMask::ImageOperandsConstOffsetMask:
    return "ImageOperandsConstOffsetMask";
  case spv::ImageOperandsMask::ImageOperandsOffsetMask:
    return "ImageOperandsOffsetMask";
  case spv::ImageOperandsMask::ImageOperandsConstOffsetsMask:
    return "ImageOperandsConstOffsetsMask";
  case spv::ImageOperandsMask::ImageOperandsSampleMask:
    return "ImageOperandsSampleMask";
  case spv::ImageOperandsMask::ImageOperandsMinLodMask:
    return "ImageOperandsMinLodMask";
  case spv::ImageOperandsMask::ImageOperandsMakeTexelAvailableMask:
    return "ImageOperandsMakeTexelAvailableMask";
    //    case spv::ImageOperandsMask::ImageOperandsMakeTexelAvailableKHRMask :
    //    return "ImageOperandsMakeTexelAvailableKHRMask";
  case spv::ImageOperandsMask::ImageOperandsMakeTexelVisibleMask:
    return "ImageOperandsMakeTexelVisibleMask";
    //    case spv::ImageOperandsMask::ImageOperandsMakeTexelVisibleKHRMask :
    //    return "ImageOperandsMakeTexelVisibleKHRMask";
  case spv::ImageOperandsMask::ImageOperandsNonPrivateTexelMask:
    return "ImageOperandsNonPrivateTexelMask";
    //    case spv::ImageOperandsMask::ImageOperandsNonPrivateTexelKHRMask :
    //    return "ImageOperandsNonPrivateTexelKHRMask";
  case spv::ImageOperandsMask::ImageOperandsVolatileTexelMask:
    return "ImageOperandsVolatileTexelMask";
    //    case spv::ImageOperandsMask::ImageOperandsVolatileTexelKHRMask :
    //    return "ImageOperandsVolatileTexelKHRMask";
  case spv::ImageOperandsMask::ImageOperandsSignExtendMask:
    return "ImageOperandsSignExtendMask";
  case spv::ImageOperandsMask::ImageOperandsZeroExtendMask:
    return "ImageOperandsZeroExtendMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::ImageOperandsMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::FPFastMathModeShift const &str) {
  switch (str) {
  case spv::FPFastMathModeShift::FPFastMathModeNotNaNShift:
    return "FPFastMathModeNotNaNShift";
  case spv::FPFastMathModeShift::FPFastMathModeNotInfShift:
    return "FPFastMathModeNotInfShift";
  case spv::FPFastMathModeShift::FPFastMathModeNSZShift:
    return "FPFastMathModeNSZShift";
  case spv::FPFastMathModeShift::FPFastMathModeAllowRecipShift:
    return "FPFastMathModeAllowRecipShift";
  case spv::FPFastMathModeShift::FPFastMathModeFastShift:
    return "FPFastMathModeFastShift";
  case spv::FPFastMathModeShift::FPFastMathModeMax:
    return "FPFastMathModeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::FPFastMathModeShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::FPFastMathModeMask const &str) {
  switch (str) {
  case spv::FPFastMathModeMask::FPFastMathModeMaskNone:
    return "FPFastMathModeMaskNone";
  case spv::FPFastMathModeMask::FPFastMathModeNotNaNMask:
    return "FPFastMathModeNotNaNMask";
  case spv::FPFastMathModeMask::FPFastMathModeNotInfMask:
    return "FPFastMathModeNotInfMask";
  case spv::FPFastMathModeMask::FPFastMathModeNSZMask:
    return "FPFastMathModeNSZMask";
  case spv::FPFastMathModeMask::FPFastMathModeAllowRecipMask:
    return "FPFastMathModeAllowRecipMask";
  case spv::FPFastMathModeMask::FPFastMathModeFastMask:
    return "FPFastMathModeFastMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::FPFastMathModeMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::FPRoundingMode const &str) {
  switch (str) {
  case spv::FPRoundingMode::FPRoundingModeRTE:
    return "FPRoundingModeRTE";
  case spv::FPRoundingMode::FPRoundingModeRTZ:
    return "FPRoundingModeRTZ";
  case spv::FPRoundingMode::FPRoundingModeRTP:
    return "FPRoundingModeRTP";
  case spv::FPRoundingMode::FPRoundingModeRTN:
    return "FPRoundingModeRTN";
  case spv::FPRoundingMode::FPRoundingModeMax:
    return "FPRoundingModeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::FPRoundingMode const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::LinkageType const &str) {
  switch (str) {
  case spv::LinkageType::LinkageTypeExport:
    return "LinkageTypeExport";
  case spv::LinkageType::LinkageTypeImport:
    return "LinkageTypeImport";
  case spv::LinkageType::LinkageTypeMax:
    return "LinkageTypeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::LinkageType const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::AccessQualifier const &str) {
  switch (str) {
  case spv::AccessQualifier::AccessQualifierReadOnly:
    return "AccessQualifierReadOnly";
  case spv::AccessQualifier::AccessQualifierWriteOnly:
    return "AccessQualifierWriteOnly";
  case spv::AccessQualifier::AccessQualifierReadWrite:
    return "AccessQualifierReadWrite";
  case spv::AccessQualifier::AccessQualifierMax:
    return "AccessQualifierMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::AccessQualifier const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::FunctionParameterAttribute const &str) {
  switch (str) {
  case spv::FunctionParameterAttribute::FunctionParameterAttributeZext:
    return "FunctionParameterAttributeZext";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeSext:
    return "FunctionParameterAttributeSext";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeByVal:
    return "FunctionParameterAttributeByVal";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeSret:
    return "FunctionParameterAttributeSret";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeNoAlias:
    return "FunctionParameterAttributeNoAlias";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeNoCapture:
    return "FunctionParameterAttributeNoCapture";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeNoWrite:
    return "FunctionParameterAttributeNoWrite";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeNoReadWrite:
    return "FunctionParameterAttributeNoReadWrite";
  case spv::FunctionParameterAttribute::FunctionParameterAttributeMax:
    return "FunctionParameterAttributeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::FunctionParameterAttribute const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::Decoration const &str) {
  switch (str) {
  case spv::Decoration::DecorationRelaxedPrecision:
    return "DecorationRelaxedPrecision";
  case spv::Decoration::DecorationSpecId:
    return "DecorationSpecId";
  case spv::Decoration::DecorationBlock:
    return "DecorationBlock";
  case spv::Decoration::DecorationBufferBlock:
    return "DecorationBufferBlock";
  case spv::Decoration::DecorationRowMajor:
    return "DecorationRowMajor";
  case spv::Decoration::DecorationColMajor:
    return "DecorationColMajor";
  case spv::Decoration::DecorationArrayStride:
    return "DecorationArrayStride";
  case spv::Decoration::DecorationMatrixStride:
    return "DecorationMatrixStride";
  case spv::Decoration::DecorationGLSLShared:
    return "DecorationGLSLShared";
  case spv::Decoration::DecorationGLSLPacked:
    return "DecorationGLSLPacked";
  case spv::Decoration::DecorationCPacked:
    return "DecorationCPacked";
  case spv::Decoration::DecorationBuiltIn:
    return "DecorationBuiltIn";
  case spv::Decoration::DecorationNoPerspective:
    return "DecorationNoPerspective";
  case spv::Decoration::DecorationFlat:
    return "DecorationFlat";
  case spv::Decoration::DecorationPatch:
    return "DecorationPatch";
  case spv::Decoration::DecorationCentroid:
    return "DecorationCentroid";
  case spv::Decoration::DecorationSample:
    return "DecorationSample";
  case spv::Decoration::DecorationInvariant:
    return "DecorationInvariant";
  case spv::Decoration::DecorationRestrict:
    return "DecorationRestrict";
  case spv::Decoration::DecorationAliased:
    return "DecorationAliased";
  case spv::Decoration::DecorationVolatile:
    return "DecorationVolatile";
  case spv::Decoration::DecorationConstant:
    return "DecorationConstant";
  case spv::Decoration::DecorationCoherent:
    return "DecorationCoherent";
  case spv::Decoration::DecorationNonWritable:
    return "DecorationNonWritable";
  case spv::Decoration::DecorationNonReadable:
    return "DecorationNonReadable";
  case spv::Decoration::DecorationUniform:
    return "DecorationUniform";
  case spv::Decoration::DecorationUniformId:
    return "DecorationUniformId";
  case spv::Decoration::DecorationSaturatedConversion:
    return "DecorationSaturatedConversion";
  case spv::Decoration::DecorationStream:
    return "DecorationStream";
  case spv::Decoration::DecorationLocation:
    return "DecorationLocation";
  case spv::Decoration::DecorationComponent:
    return "DecorationComponent";
  case spv::Decoration::DecorationIndex:
    return "DecorationIndex";
  case spv::Decoration::DecorationBinding:
    return "DecorationBinding";
  case spv::Decoration::DecorationDescriptorSet:
    return "DecorationDescriptorSet";
  case spv::Decoration::DecorationOffset:
    return "DecorationOffset";
  case spv::Decoration::DecorationXfbBuffer:
    return "DecorationXfbBuffer";
  case spv::Decoration::DecorationXfbStride:
    return "DecorationXfbStride";
  case spv::Decoration::DecorationFuncParamAttr:
    return "DecorationFuncParamAttr";
  case spv::Decoration::DecorationFPRoundingMode:
    return "DecorationFPRoundingMode";
  case spv::Decoration::DecorationFPFastMathMode:
    return "DecorationFPFastMathMode";
  case spv::Decoration::DecorationLinkageAttributes:
    return "DecorationLinkageAttributes";
  case spv::Decoration::DecorationNoContraction:
    return "DecorationNoContraction";
  case spv::Decoration::DecorationInputAttachmentIndex:
    return "DecorationInputAttachmentIndex";
  case spv::Decoration::DecorationAlignment:
    return "DecorationAlignment";
  case spv::Decoration::DecorationMaxByteOffset:
    return "DecorationMaxByteOffset";
  case spv::Decoration::DecorationAlignmentId:
    return "DecorationAlignmentId";
  case spv::Decoration::DecorationMaxByteOffsetId:
    return "DecorationMaxByteOffsetId";
  case spv::Decoration::DecorationNoSignedWrap:
    return "DecorationNoSignedWrap";
  case spv::Decoration::DecorationNoUnsignedWrap:
    return "DecorationNoUnsignedWrap";
  case spv::Decoration::DecorationExplicitInterpAMD:
    return "DecorationExplicitInterpAMD";
  case spv::Decoration::DecorationOverrideCoverageNV:
    return "DecorationOverrideCoverageNV";
  case spv::Decoration::DecorationPassthroughNV:
    return "DecorationPassthroughNV";
  case spv::Decoration::DecorationViewportRelativeNV:
    return "DecorationViewportRelativeNV";
  case spv::Decoration::DecorationSecondaryViewportRelativeNV:
    return "DecorationSecondaryViewportRelativeNV";
  case spv::Decoration::DecorationPerPrimitiveNV:
    return "DecorationPerPrimitiveNV";
  case spv::Decoration::DecorationPerViewNV:
    return "DecorationPerViewNV";
  case spv::Decoration::DecorationPerTaskNV:
    return "DecorationPerTaskNV";
  case spv::Decoration::DecorationPerVertexNV:
    return "DecorationPerVertexNV";
  case spv::Decoration::DecorationNonUniform:
    return "DecorationNonUniform";
    //    case spv::Decoration::DecorationNonUniformEXT : return
    //    "DecorationNonUniformEXT";
  case spv::Decoration::DecorationRestrictPointer:
    return "DecorationRestrictPointer";
    //    case spv::Decoration::DecorationRestrictPointerEXT : return
    //    "DecorationRestrictPointerEXT";
  case spv::Decoration::DecorationAliasedPointer:
    return "DecorationAliasedPointer";
    //    case spv::Decoration::DecorationAliasedPointerEXT : return
    //    "DecorationAliasedPointerEXT";
  case spv::Decoration::DecorationCounterBuffer:
    return "DecorationCounterBuffer";
    //    case spv::Decoration::DecorationHlslCounterBufferGOOGLE : return
    //    "DecorationHlslCounterBufferGOOGLE";
  case spv::Decoration::DecorationHlslSemanticGOOGLE:
    return "DecorationHlslSemanticGOOGLE";
    //    case spv::Decoration::DecorationUserSemantic : return
    //    "DecorationUserSemantic";
  case spv::Decoration::DecorationUserTypeGOOGLE:
    return "DecorationUserTypeGOOGLE";
  case spv::Decoration::DecorationMax:
    return "DecorationMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::Decoration const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::BuiltIn const &str) {
  switch (str) {
  case spv::BuiltIn::BuiltInPosition:
    return "BuiltInPosition";
  case spv::BuiltIn::BuiltInPointSize:
    return "BuiltInPointSize";
  case spv::BuiltIn::BuiltInClipDistance:
    return "BuiltInClipDistance";
  case spv::BuiltIn::BuiltInCullDistance:
    return "BuiltInCullDistance";
  case spv::BuiltIn::BuiltInVertexId:
    return "BuiltInVertexId";
  case spv::BuiltIn::BuiltInInstanceId:
    return "BuiltInInstanceId";
  case spv::BuiltIn::BuiltInPrimitiveId:
    return "BuiltInPrimitiveId";
  case spv::BuiltIn::BuiltInInvocationId:
    return "BuiltInInvocationId";
  case spv::BuiltIn::BuiltInLayer:
    return "BuiltInLayer";
  case spv::BuiltIn::BuiltInViewportIndex:
    return "BuiltInViewportIndex";
  case spv::BuiltIn::BuiltInTessLevelOuter:
    return "BuiltInTessLevelOuter";
  case spv::BuiltIn::BuiltInTessLevelInner:
    return "BuiltInTessLevelInner";
  case spv::BuiltIn::BuiltInTessCoord:
    return "BuiltInTessCoord";
  case spv::BuiltIn::BuiltInPatchVertices:
    return "BuiltInPatchVertices";
  case spv::BuiltIn::BuiltInFragCoord:
    return "BuiltInFragCoord";
  case spv::BuiltIn::BuiltInPointCoord:
    return "BuiltInPointCoord";
  case spv::BuiltIn::BuiltInFrontFacing:
    return "BuiltInFrontFacing";
  case spv::BuiltIn::BuiltInSampleId:
    return "BuiltInSampleId";
  case spv::BuiltIn::BuiltInSamplePosition:
    return "BuiltInSamplePosition";
  case spv::BuiltIn::BuiltInSampleMask:
    return "BuiltInSampleMask";
  case spv::BuiltIn::BuiltInFragDepth:
    return "BuiltInFragDepth";
  case spv::BuiltIn::BuiltInHelperInvocation:
    return "BuiltInHelperInvocation";
  case spv::BuiltIn::BuiltInNumWorkgroups:
    return "BuiltInNumWorkgroups";
  case spv::BuiltIn::BuiltInWorkgroupSize:
    return "BuiltInWorkgroupSize";
  case spv::BuiltIn::BuiltInWorkgroupId:
    return "BuiltInWorkgroupId";
  case spv::BuiltIn::BuiltInLocalInvocationId:
    return "BuiltInLocalInvocationId";
  case spv::BuiltIn::BuiltInGlobalInvocationId:
    return "BuiltInGlobalInvocationId";
  case spv::BuiltIn::BuiltInLocalInvocationIndex:
    return "BuiltInLocalInvocationIndex";
  case spv::BuiltIn::BuiltInWorkDim:
    return "BuiltInWorkDim";
  case spv::BuiltIn::BuiltInGlobalSize:
    return "BuiltInGlobalSize";
  case spv::BuiltIn::BuiltInEnqueuedWorkgroupSize:
    return "BuiltInEnqueuedWorkgroupSize";
  case spv::BuiltIn::BuiltInGlobalOffset:
    return "BuiltInGlobalOffset";
  case spv::BuiltIn::BuiltInGlobalLinearId:
    return "BuiltInGlobalLinearId";
  case spv::BuiltIn::BuiltInSubgroupSize:
    return "BuiltInSubgroupSize";
  case spv::BuiltIn::BuiltInSubgroupMaxSize:
    return "BuiltInSubgroupMaxSize";
  case spv::BuiltIn::BuiltInNumSubgroups:
    return "BuiltInNumSubgroups";
  case spv::BuiltIn::BuiltInNumEnqueuedSubgroups:
    return "BuiltInNumEnqueuedSubgroups";
  case spv::BuiltIn::BuiltInSubgroupId:
    return "BuiltInSubgroupId";
  case spv::BuiltIn::BuiltInSubgroupLocalInvocationId:
    return "BuiltInSubgroupLocalInvocationId";
  case spv::BuiltIn::BuiltInVertexIndex:
    return "BuiltInVertexIndex";
  case spv::BuiltIn::BuiltInInstanceIndex:
    return "BuiltInInstanceIndex";
  case spv::BuiltIn::BuiltInSubgroupEqMask:
    return "BuiltInSubgroupEqMask";
    //    case spv::BuiltIn::BuiltInSubgroupEqMaskKHR : return
    //    "BuiltInSubgroupEqMaskKHR";
  case spv::BuiltIn::BuiltInSubgroupGeMask:
    return "BuiltInSubgroupGeMask";
    //    case spv::BuiltIn::BuiltInSubgroupGeMaskKHR : return
    //    "BuiltInSubgroupGeMaskKHR";
  case spv::BuiltIn::BuiltInSubgroupGtMask:
    return "BuiltInSubgroupGtMask";
    //    case spv::BuiltIn::BuiltInSubgroupGtMaskKHR : return
    //    "BuiltInSubgroupGtMaskKHR";
  case spv::BuiltIn::BuiltInSubgroupLeMask:
    return "BuiltInSubgroupLeMask";
    //    case spv::BuiltIn::BuiltInSubgroupLeMaskKHR : return
    //    "BuiltInSubgroupLeMaskKHR";
  case spv::BuiltIn::BuiltInSubgroupLtMask:
    return "BuiltInSubgroupLtMask";
    //    case spv::BuiltIn::BuiltInSubgroupLtMaskKHR : return
    //    "BuiltInSubgroupLtMaskKHR";
  case spv::BuiltIn::BuiltInBaseVertex:
    return "BuiltInBaseVertex";
  case spv::BuiltIn::BuiltInBaseInstance:
    return "BuiltInBaseInstance";
  case spv::BuiltIn::BuiltInDrawIndex:
    return "BuiltInDrawIndex";
  case spv::BuiltIn::BuiltInDeviceIndex:
    return "BuiltInDeviceIndex";
  case spv::BuiltIn::BuiltInViewIndex:
    return "BuiltInViewIndex";
  case spv::BuiltIn::BuiltInBaryCoordNoPerspAMD:
    return "BuiltInBaryCoordNoPerspAMD";
  case spv::BuiltIn::BuiltInBaryCoordNoPerspCentroidAMD:
    return "BuiltInBaryCoordNoPerspCentroidAMD";
  case spv::BuiltIn::BuiltInBaryCoordNoPerspSampleAMD:
    return "BuiltInBaryCoordNoPerspSampleAMD";
  case spv::BuiltIn::BuiltInBaryCoordSmoothAMD:
    return "BuiltInBaryCoordSmoothAMD";
  case spv::BuiltIn::BuiltInBaryCoordSmoothCentroidAMD:
    return "BuiltInBaryCoordSmoothCentroidAMD";
  case spv::BuiltIn::BuiltInBaryCoordSmoothSampleAMD:
    return "BuiltInBaryCoordSmoothSampleAMD";
  case spv::BuiltIn::BuiltInBaryCoordPullModelAMD:
    return "BuiltInBaryCoordPullModelAMD";
  case spv::BuiltIn::BuiltInFragStencilRefEXT:
    return "BuiltInFragStencilRefEXT";
  case spv::BuiltIn::BuiltInViewportMaskNV:
    return "BuiltInViewportMaskNV";
  case spv::BuiltIn::BuiltInSecondaryPositionNV:
    return "BuiltInSecondaryPositionNV";
  case spv::BuiltIn::BuiltInSecondaryViewportMaskNV:
    return "BuiltInSecondaryViewportMaskNV";
  case spv::BuiltIn::BuiltInPositionPerViewNV:
    return "BuiltInPositionPerViewNV";
  case spv::BuiltIn::BuiltInViewportMaskPerViewNV:
    return "BuiltInViewportMaskPerViewNV";
  case spv::BuiltIn::BuiltInFullyCoveredEXT:
    return "BuiltInFullyCoveredEXT";
  case spv::BuiltIn::BuiltInTaskCountNV:
    return "BuiltInTaskCountNV";
  case spv::BuiltIn::BuiltInPrimitiveCountNV:
    return "BuiltInPrimitiveCountNV";
  case spv::BuiltIn::BuiltInPrimitiveIndicesNV:
    return "BuiltInPrimitiveIndicesNV";
  case spv::BuiltIn::BuiltInClipDistancePerViewNV:
    return "BuiltInClipDistancePerViewNV";
  case spv::BuiltIn::BuiltInCullDistancePerViewNV:
    return "BuiltInCullDistancePerViewNV";
  case spv::BuiltIn::BuiltInLayerPerViewNV:
    return "BuiltInLayerPerViewNV";
  case spv::BuiltIn::BuiltInMeshViewCountNV:
    return "BuiltInMeshViewCountNV";
  case spv::BuiltIn::BuiltInMeshViewIndicesNV:
    return "BuiltInMeshViewIndicesNV";
  case spv::BuiltIn::BuiltInBaryCoordNV:
    return "BuiltInBaryCoordNV";
  case spv::BuiltIn::BuiltInBaryCoordNoPerspNV:
    return "BuiltInBaryCoordNoPerspNV";
  case spv::BuiltIn::BuiltInFragSizeEXT:
    return "BuiltInFragSizeEXT";
    //    case spv::BuiltIn::BuiltInFragmentSizeNV : return
    //    "BuiltInFragmentSizeNV";
  case spv::BuiltIn::BuiltInFragInvocationCountEXT:
    return "BuiltInFragInvocationCountEXT";
    //    case spv::BuiltIn::BuiltInInvocationsPerPixelNV : return
    //    "BuiltInInvocationsPerPixelNV";
  case spv::BuiltIn::BuiltInLaunchIdNV:
    return "BuiltInLaunchIdNV";
  case spv::BuiltIn::BuiltInLaunchSizeNV:
    return "BuiltInLaunchSizeNV";
  case spv::BuiltIn::BuiltInWorldRayOriginNV:
    return "BuiltInWorldRayOriginNV";
  case spv::BuiltIn::BuiltInWorldRayDirectionNV:
    return "BuiltInWorldRayDirectionNV";
  case spv::BuiltIn::BuiltInObjectRayOriginNV:
    return "BuiltInObjectRayOriginNV";
  case spv::BuiltIn::BuiltInObjectRayDirectionNV:
    return "BuiltInObjectRayDirectionNV";
  case spv::BuiltIn::BuiltInRayTminNV:
    return "BuiltInRayTminNV";
  case spv::BuiltIn::BuiltInRayTmaxNV:
    return "BuiltInRayTmaxNV";
  case spv::BuiltIn::BuiltInInstanceCustomIndexNV:
    return "BuiltInInstanceCustomIndexNV";
  case spv::BuiltIn::BuiltInObjectToWorldNV:
    return "BuiltInObjectToWorldNV";
  case spv::BuiltIn::BuiltInWorldToObjectNV:
    return "BuiltInWorldToObjectNV";
  case spv::BuiltIn::BuiltInHitTNV:
    return "BuiltInHitTNV";
  case spv::BuiltIn::BuiltInHitKindNV:
    return "BuiltInHitKindNV";
  case spv::BuiltIn::BuiltInIncomingRayFlagsNV:
    return "BuiltInIncomingRayFlagsNV";
  case spv::BuiltIn::BuiltInWarpsPerSMNV:
    return "BuiltInWarpsPerSMNV";
  case spv::BuiltIn::BuiltInSMCountNV:
    return "BuiltInSMCountNV";
  case spv::BuiltIn::BuiltInWarpIDNV:
    return "BuiltInWarpIDNV";
  case spv::BuiltIn::BuiltInSMIDNV:
    return "BuiltInSMIDNV";
  case spv::BuiltIn::BuiltInMax:
    return "BuiltInMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::BuiltIn const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::SelectionControlShift const &str) {
  switch (str) {
  case spv::SelectionControlShift::SelectionControlFlattenShift:
    return "SelectionControlFlattenShift";
  case spv::SelectionControlShift::SelectionControlDontFlattenShift:
    return "SelectionControlDontFlattenShift";
  case spv::SelectionControlShift::SelectionControlMax:
    return "SelectionControlMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::SelectionControlShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::SelectionControlMask const &str) {
  switch (str) {
  case spv::SelectionControlMask::SelectionControlMaskNone:
    return "SelectionControlMaskNone";
  case spv::SelectionControlMask::SelectionControlFlattenMask:
    return "SelectionControlFlattenMask";
  case spv::SelectionControlMask::SelectionControlDontFlattenMask:
    return "SelectionControlDontFlattenMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::SelectionControlMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::LoopControlShift const &str) {
  switch (str) {
  case spv::LoopControlShift::LoopControlUnrollShift:
    return "LoopControlUnrollShift";
  case spv::LoopControlShift::LoopControlDontUnrollShift:
    return "LoopControlDontUnrollShift";
  case spv::LoopControlShift::LoopControlDependencyInfiniteShift:
    return "LoopControlDependencyInfiniteShift";
  case spv::LoopControlShift::LoopControlDependencyLengthShift:
    return "LoopControlDependencyLengthShift";
  case spv::LoopControlShift::LoopControlMinIterationsShift:
    return "LoopControlMinIterationsShift";
  case spv::LoopControlShift::LoopControlMaxIterationsShift:
    return "LoopControlMaxIterationsShift";
  case spv::LoopControlShift::LoopControlIterationMultipleShift:
    return "LoopControlIterationMultipleShift";
  case spv::LoopControlShift::LoopControlPeelCountShift:
    return "LoopControlPeelCountShift";
  case spv::LoopControlShift::LoopControlPartialCountShift:
    return "LoopControlPartialCountShift";
  case spv::LoopControlShift::LoopControlMax:
    return "LoopControlMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::LoopControlShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::LoopControlMask const &str) {
  switch (str) {
  case spv::LoopControlMask::LoopControlMaskNone:
    return "LoopControlMaskNone";
  case spv::LoopControlMask::LoopControlUnrollMask:
    return "LoopControlUnrollMask";
  case spv::LoopControlMask::LoopControlDontUnrollMask:
    return "LoopControlDontUnrollMask";
  case spv::LoopControlMask::LoopControlDependencyInfiniteMask:
    return "LoopControlDependencyInfiniteMask";
  case spv::LoopControlMask::LoopControlDependencyLengthMask:
    return "LoopControlDependencyLengthMask";
  case spv::LoopControlMask::LoopControlMinIterationsMask:
    return "LoopControlMinIterationsMask";
  case spv::LoopControlMask::LoopControlMaxIterationsMask:
    return "LoopControlMaxIterationsMask";
  case spv::LoopControlMask::LoopControlIterationMultipleMask:
    return "LoopControlIterationMultipleMask";
  case spv::LoopControlMask::LoopControlPeelCountMask:
    return "LoopControlPeelCountMask";
  case spv::LoopControlMask::LoopControlPartialCountMask:
    return "LoopControlPartialCountMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::LoopControlMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::FunctionControlShift const &str) {
  switch (str) {
  case spv::FunctionControlShift::FunctionControlInlineShift:
    return "FunctionControlInlineShift";
  case spv::FunctionControlShift::FunctionControlDontInlineShift:
    return "FunctionControlDontInlineShift";
  case spv::FunctionControlShift::FunctionControlPureShift:
    return "FunctionControlPureShift";
  case spv::FunctionControlShift::FunctionControlConstShift:
    return "FunctionControlConstShift";
  case spv::FunctionControlShift::FunctionControlMax:
    return "FunctionControlMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::FunctionControlShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::FunctionControlMask const &str) {
  switch (str) {
  case spv::FunctionControlMask::FunctionControlMaskNone:
    return "FunctionControlMaskNone";
  case spv::FunctionControlMask::FunctionControlInlineMask:
    return "FunctionControlInlineMask";
  case spv::FunctionControlMask::FunctionControlDontInlineMask:
    return "FunctionControlDontInlineMask";
  case spv::FunctionControlMask::FunctionControlPureMask:
    return "FunctionControlPureMask";
  case spv::FunctionControlMask::FunctionControlConstMask:
    return "FunctionControlConstMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::FunctionControlMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::MemorySemanticsShift const &str) {
  switch (str) {
  case spv::MemorySemanticsShift::MemorySemanticsAcquireShift:
    return "MemorySemanticsAcquireShift";
  case spv::MemorySemanticsShift::MemorySemanticsReleaseShift:
    return "MemorySemanticsReleaseShift";
  case spv::MemorySemanticsShift::MemorySemanticsAcquireReleaseShift:
    return "MemorySemanticsAcquireReleaseShift";
  case spv::MemorySemanticsShift::MemorySemanticsSequentiallyConsistentShift:
    return "MemorySemanticsSequentiallyConsistentShift";
  case spv::MemorySemanticsShift::MemorySemanticsUniformMemoryShift:
    return "MemorySemanticsUniformMemoryShift";
  case spv::MemorySemanticsShift::MemorySemanticsSubgroupMemoryShift:
    return "MemorySemanticsSubgroupMemoryShift";
  case spv::MemorySemanticsShift::MemorySemanticsWorkgroupMemoryShift:
    return "MemorySemanticsWorkgroupMemoryShift";
  case spv::MemorySemanticsShift::MemorySemanticsCrossWorkgroupMemoryShift:
    return "MemorySemanticsCrossWorkgroupMemoryShift";
  case spv::MemorySemanticsShift::MemorySemanticsAtomicCounterMemoryShift:
    return "MemorySemanticsAtomicCounterMemoryShift";
  case spv::MemorySemanticsShift::MemorySemanticsImageMemoryShift:
    return "MemorySemanticsImageMemoryShift";
  case spv::MemorySemanticsShift::MemorySemanticsOutputMemoryShift:
    return "MemorySemanticsOutputMemoryShift";
    //    case spv::MemorySemanticsShift::MemorySemanticsOutputMemoryKHRShift :
    //    return "MemorySemanticsOutputMemoryKHRShift";
  case spv::MemorySemanticsShift::MemorySemanticsMakeAvailableShift:
    return "MemorySemanticsMakeAvailableShift";
    //    case spv::MemorySemanticsShift::MemorySemanticsMakeAvailableKHRShift :
    //    return "MemorySemanticsMakeAvailableKHRShift";
  case spv::MemorySemanticsShift::MemorySemanticsMakeVisibleShift:
    return "MemorySemanticsMakeVisibleShift";
    //    case spv::MemorySemanticsShift::MemorySemanticsMakeVisibleKHRShift :
    //    return "MemorySemanticsMakeVisibleKHRShift";
  case spv::MemorySemanticsShift::MemorySemanticsVolatileShift:
    return "MemorySemanticsVolatileShift";
  case spv::MemorySemanticsShift::MemorySemanticsMax:
    return "MemorySemanticsMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::MemorySemanticsShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::MemorySemanticsMask const &str) {
  switch (str) {
  case spv::MemorySemanticsMask::MemorySemanticsMaskNone:
    return "MemorySemanticsMaskNone";
  case spv::MemorySemanticsMask::MemorySemanticsAcquireMask:
    return "MemorySemanticsAcquireMask";
  case spv::MemorySemanticsMask::MemorySemanticsReleaseMask:
    return "MemorySemanticsReleaseMask";
  case spv::MemorySemanticsMask::MemorySemanticsAcquireReleaseMask:
    return "MemorySemanticsAcquireReleaseMask";
  case spv::MemorySemanticsMask::MemorySemanticsSequentiallyConsistentMask:
    return "MemorySemanticsSequentiallyConsistentMask";
  case spv::MemorySemanticsMask::MemorySemanticsUniformMemoryMask:
    return "MemorySemanticsUniformMemoryMask";
  case spv::MemorySemanticsMask::MemorySemanticsSubgroupMemoryMask:
    return "MemorySemanticsSubgroupMemoryMask";
  case spv::MemorySemanticsMask::MemorySemanticsWorkgroupMemoryMask:
    return "MemorySemanticsWorkgroupMemoryMask";
  case spv::MemorySemanticsMask::MemorySemanticsCrossWorkgroupMemoryMask:
    return "MemorySemanticsCrossWorkgroupMemoryMask";
  case spv::MemorySemanticsMask::MemorySemanticsAtomicCounterMemoryMask:
    return "MemorySemanticsAtomicCounterMemoryMask";
  case spv::MemorySemanticsMask::MemorySemanticsImageMemoryMask:
    return "MemorySemanticsImageMemoryMask";
  case spv::MemorySemanticsMask::MemorySemanticsOutputMemoryMask:
    return "MemorySemanticsOutputMemoryMask";
    //    case spv::MemorySemanticsMask::MemorySemanticsOutputMemoryKHRMask :
    //    return "MemorySemanticsOutputMemoryKHRMask";
  case spv::MemorySemanticsMask::MemorySemanticsMakeAvailableMask:
    return "MemorySemanticsMakeAvailableMask";
    //    case spv::MemorySemanticsMask::MemorySemanticsMakeAvailableKHRMask :
    //    return "MemorySemanticsMakeAvailableKHRMask";
  case spv::MemorySemanticsMask::MemorySemanticsMakeVisibleMask:
    return "MemorySemanticsMakeVisibleMask";
    //    case spv::MemorySemanticsMask::MemorySemanticsMakeVisibleKHRMask :
    //    return "MemorySemanticsMakeVisibleKHRMask";
  case spv::MemorySemanticsMask::MemorySemanticsVolatileMask:
    return "MemorySemanticsVolatileMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::MemorySemanticsMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::MemoryAccessShift const &str) {
  switch (str) {
  case spv::MemoryAccessShift::MemoryAccessVolatileShift:
    return "MemoryAccessVolatileShift";
  case spv::MemoryAccessShift::MemoryAccessAlignedShift:
    return "MemoryAccessAlignedShift";
  case spv::MemoryAccessShift::MemoryAccessNontemporalShift:
    return "MemoryAccessNontemporalShift";
  case spv::MemoryAccessShift::MemoryAccessMakePointerAvailableShift:
    return "MemoryAccessMakePointerAvailableShift";
    //    case spv::MemoryAccessShift::MemoryAccessMakePointerAvailableKHRShift
    //    : return "MemoryAccessMakePointerAvailableKHRShift";
  case spv::MemoryAccessShift::MemoryAccessMakePointerVisibleShift:
    return "MemoryAccessMakePointerVisibleShift";
    //    case spv::MemoryAccessShift::MemoryAccessMakePointerVisibleKHRShift :
    //    return "MemoryAccessMakePointerVisibleKHRShift";
  case spv::MemoryAccessShift::MemoryAccessNonPrivatePointerShift:
    return "MemoryAccessNonPrivatePointerShift";
    //    case spv::MemoryAccessShift::MemoryAccessNonPrivatePointerKHRShift :
    //    return "MemoryAccessNonPrivatePointerKHRShift";
  case spv::MemoryAccessShift::MemoryAccessMax:
    return "MemoryAccessMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::MemoryAccessShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::MemoryAccessMask const &str) {
  switch (str) {
  case spv::MemoryAccessMask::MemoryAccessMaskNone:
    return "MemoryAccessMaskNone";
  case spv::MemoryAccessMask::MemoryAccessVolatileMask:
    return "MemoryAccessVolatileMask";
  case spv::MemoryAccessMask::MemoryAccessAlignedMask:
    return "MemoryAccessAlignedMask";
  case spv::MemoryAccessMask::MemoryAccessNontemporalMask:
    return "MemoryAccessNontemporalMask";
  case spv::MemoryAccessMask::MemoryAccessMakePointerAvailableMask:
    return "MemoryAccessMakePointerAvailableMask";
    //    case spv::MemoryAccessMask::MemoryAccessMakePointerAvailableKHRMask :
    //    return "MemoryAccessMakePointerAvailableKHRMask";
  case spv::MemoryAccessMask::MemoryAccessMakePointerVisibleMask:
    return "MemoryAccessMakePointerVisibleMask";
    //    case spv::MemoryAccessMask::MemoryAccessMakePointerVisibleKHRMask :
    //    return "MemoryAccessMakePointerVisibleKHRMask";
  case spv::MemoryAccessMask::MemoryAccessNonPrivatePointerMask:
    return "MemoryAccessNonPrivatePointerMask";
    //    case spv::MemoryAccessMask::MemoryAccessNonPrivatePointerKHRMask :
    //    return "MemoryAccessNonPrivatePointerKHRMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::MemoryAccessMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::Scope const &str) {
  switch (str) {
  case spv::Scope::ScopeCrossDevice:
    return "ScopeCrossDevice";
  case spv::Scope::ScopeDevice:
    return "ScopeDevice";
  case spv::Scope::ScopeWorkgroup:
    return "ScopeWorkgroup";
  case spv::Scope::ScopeSubgroup:
    return "ScopeSubgroup";
  case spv::Scope::ScopeInvocation:
    return "ScopeInvocation";
  case spv::Scope::ScopeQueueFamily:
    return "ScopeQueueFamily";
    //    case spv::Scope::ScopeQueueFamilyKHR : return "ScopeQueueFamilyKHR";
  case spv::Scope::ScopeMax:
    return "ScopeMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::Scope const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::GroupOperation const &str) {
  switch (str) {
  case spv::GroupOperation::GroupOperationReduce:
    return "GroupOperationReduce";
  case spv::GroupOperation::GroupOperationInclusiveScan:
    return "GroupOperationInclusiveScan";
  case spv::GroupOperation::GroupOperationExclusiveScan:
    return "GroupOperationExclusiveScan";
  case spv::GroupOperation::GroupOperationClusteredReduce:
    return "GroupOperationClusteredReduce";
  case spv::GroupOperation::GroupOperationPartitionedReduceNV:
    return "GroupOperationPartitionedReduceNV";
  case spv::GroupOperation::GroupOperationPartitionedInclusiveScanNV:
    return "GroupOperationPartitionedInclusiveScanNV";
  case spv::GroupOperation::GroupOperationPartitionedExclusiveScanNV:
    return "GroupOperationPartitionedExclusiveScanNV";
  case spv::GroupOperation::GroupOperationMax:
    return "GroupOperationMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::GroupOperation const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::KernelEnqueueFlags const &str) {
  switch (str) {
  case spv::KernelEnqueueFlags::KernelEnqueueFlagsNoWait:
    return "KernelEnqueueFlagsNoWait";
  case spv::KernelEnqueueFlags::KernelEnqueueFlagsWaitKernel:
    return "KernelEnqueueFlagsWaitKernel";
  case spv::KernelEnqueueFlags::KernelEnqueueFlagsWaitWorkGroup:
    return "KernelEnqueueFlagsWaitWorkGroup";
  case spv::KernelEnqueueFlags::KernelEnqueueFlagsMax:
    return "KernelEnqueueFlagsMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::KernelEnqueueFlags const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::KernelProfilingInfoShift const &str) {
  switch (str) {
  case spv::KernelProfilingInfoShift::KernelProfilingInfoCmdExecTimeShift:
    return "KernelProfilingInfoCmdExecTimeShift";
  case spv::KernelProfilingInfoShift::KernelProfilingInfoMax:
    return "KernelProfilingInfoMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::KernelProfilingInfoShift const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::KernelProfilingInfoMask const &str) {
  switch (str) {
  case spv::KernelProfilingInfoMask::KernelProfilingInfoMaskNone:
    return "KernelProfilingInfoMaskNone";
  case spv::KernelProfilingInfoMask::KernelProfilingInfoCmdExecTimeMask:
    return "KernelProfilingInfoCmdExecTimeMask";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::KernelProfilingInfoMask const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::Capability const &str) {
  switch (str) {
  case spv::Capability::CapabilityMatrix:
    return "CapabilityMatrix";
  case spv::Capability::CapabilityShader:
    return "CapabilityShader";
  case spv::Capability::CapabilityGeometry:
    return "CapabilityGeometry";
  case spv::Capability::CapabilityTessellation:
    return "CapabilityTessellation";
  case spv::Capability::CapabilityAddresses:
    return "CapabilityAddresses";
  case spv::Capability::CapabilityLinkage:
    return "CapabilityLinkage";
  case spv::Capability::CapabilityKernel:
    return "CapabilityKernel";
  case spv::Capability::CapabilityVector16:
    return "CapabilityVector16";
  case spv::Capability::CapabilityFloat16Buffer:
    return "CapabilityFloat16Buffer";
  case spv::Capability::CapabilityFloat16:
    return "CapabilityFloat16";
  case spv::Capability::CapabilityFloat64:
    return "CapabilityFloat64";
  case spv::Capability::CapabilityInt64:
    return "CapabilityInt64";
  case spv::Capability::CapabilityInt64Atomics:
    return "CapabilityInt64Atomics";
  case spv::Capability::CapabilityImageBasic:
    return "CapabilityImageBasic";
  case spv::Capability::CapabilityImageReadWrite:
    return "CapabilityImageReadWrite";
  case spv::Capability::CapabilityImageMipmap:
    return "CapabilityImageMipmap";
  case spv::Capability::CapabilityPipes:
    return "CapabilityPipes";
  case spv::Capability::CapabilityGroups:
    return "CapabilityGroups";
  case spv::Capability::CapabilityDeviceEnqueue:
    return "CapabilityDeviceEnqueue";
  case spv::Capability::CapabilityLiteralSampler:
    return "CapabilityLiteralSampler";
  case spv::Capability::CapabilityAtomicStorage:
    return "CapabilityAtomicStorage";
  case spv::Capability::CapabilityInt16:
    return "CapabilityInt16";
  case spv::Capability::CapabilityTessellationPointSize:
    return "CapabilityTessellationPointSize";
  case spv::Capability::CapabilityGeometryPointSize:
    return "CapabilityGeometryPointSize";
  case spv::Capability::CapabilityImageGatherExtended:
    return "CapabilityImageGatherExtended";
  case spv::Capability::CapabilityStorageImageMultisample:
    return "CapabilityStorageImageMultisample";
  case spv::Capability::CapabilityUniformBufferArrayDynamicIndexing:
    return "CapabilityUniformBufferArrayDynamicIndexing";
  case spv::Capability::CapabilitySampledImageArrayDynamicIndexing:
    return "CapabilitySampledImageArrayDynamicIndexing";
  case spv::Capability::CapabilityStorageBufferArrayDynamicIndexing:
    return "CapabilityStorageBufferArrayDynamicIndexing";
  case spv::Capability::CapabilityStorageImageArrayDynamicIndexing:
    return "CapabilityStorageImageArrayDynamicIndexing";
  case spv::Capability::CapabilityClipDistance:
    return "CapabilityClipDistance";
  case spv::Capability::CapabilityCullDistance:
    return "CapabilityCullDistance";
  case spv::Capability::CapabilityImageCubeArray:
    return "CapabilityImageCubeArray";
  case spv::Capability::CapabilitySampleRateShading:
    return "CapabilitySampleRateShading";
  case spv::Capability::CapabilityImageRect:
    return "CapabilityImageRect";
  case spv::Capability::CapabilitySampledRect:
    return "CapabilitySampledRect";
  case spv::Capability::CapabilityGenericPointer:
    return "CapabilityGenericPointer";
  case spv::Capability::CapabilityInt8:
    return "CapabilityInt8";
  case spv::Capability::CapabilityInputAttachment:
    return "CapabilityInputAttachment";
  case spv::Capability::CapabilitySparseResidency:
    return "CapabilitySparseResidency";
  case spv::Capability::CapabilityMinLod:
    return "CapabilityMinLod";
  case spv::Capability::CapabilitySampled1D:
    return "CapabilitySampled1D";
  case spv::Capability::CapabilityImage1D:
    return "CapabilityImage1D";
  case spv::Capability::CapabilitySampledCubeArray:
    return "CapabilitySampledCubeArray";
  case spv::Capability::CapabilitySampledBuffer:
    return "CapabilitySampledBuffer";
  case spv::Capability::CapabilityImageBuffer:
    return "CapabilityImageBuffer";
  case spv::Capability::CapabilityImageMSArray:
    return "CapabilityImageMSArray";
  case spv::Capability::CapabilityStorageImageExtendedFormats:
    return "CapabilityStorageImageExtendedFormats";
  case spv::Capability::CapabilityImageQuery:
    return "CapabilityImageQuery";
  case spv::Capability::CapabilityDerivativeControl:
    return "CapabilityDerivativeControl";
  case spv::Capability::CapabilityInterpolationFunction:
    return "CapabilityInterpolationFunction";
  case spv::Capability::CapabilityTransformFeedback:
    return "CapabilityTransformFeedback";
  case spv::Capability::CapabilityGeometryStreams:
    return "CapabilityGeometryStreams";
  case spv::Capability::CapabilityStorageImageReadWithoutFormat:
    return "CapabilityStorageImageReadWithoutFormat";
  case spv::Capability::CapabilityStorageImageWriteWithoutFormat:
    return "CapabilityStorageImageWriteWithoutFormat";
  case spv::Capability::CapabilityMultiViewport:
    return "CapabilityMultiViewport";
  case spv::Capability::CapabilitySubgroupDispatch:
    return "CapabilitySubgroupDispatch";
  case spv::Capability::CapabilityNamedBarrier:
    return "CapabilityNamedBarrier";
  case spv::Capability::CapabilityPipeStorage:
    return "CapabilityPipeStorage";
  case spv::Capability::CapabilityGroupNonUniform:
    return "CapabilityGroupNonUniform";
  case spv::Capability::CapabilityGroupNonUniformVote:
    return "CapabilityGroupNonUniformVote";
  case spv::Capability::CapabilityGroupNonUniformArithmetic:
    return "CapabilityGroupNonUniformArithmetic";
  case spv::Capability::CapabilityGroupNonUniformBallot:
    return "CapabilityGroupNonUniformBallot";
  case spv::Capability::CapabilityGroupNonUniformShuffle:
    return "CapabilityGroupNonUniformShuffle";
  case spv::Capability::CapabilityGroupNonUniformShuffleRelative:
    return "CapabilityGroupNonUniformShuffleRelative";
  case spv::Capability::CapabilityGroupNonUniformClustered:
    return "CapabilityGroupNonUniformClustered";
  case spv::Capability::CapabilityGroupNonUniformQuad:
    return "CapabilityGroupNonUniformQuad";
  case spv::Capability::CapabilityShaderLayer:
    return "CapabilityShaderLayer";
  case spv::Capability::CapabilityShaderViewportIndex:
    return "CapabilityShaderViewportIndex";
  case spv::Capability::CapabilitySubgroupBallotKHR:
    return "CapabilitySubgroupBallotKHR";
  case spv::Capability::CapabilityDrawParameters:
    return "CapabilityDrawParameters";
  case spv::Capability::CapabilitySubgroupVoteKHR:
    return "CapabilitySubgroupVoteKHR";
  case spv::Capability::CapabilityStorageBuffer16BitAccess:
    return "CapabilityStorageBuffer16BitAccess";
    //    case spv::Capability::CapabilityStorageUniformBufferBlock16 : return
    //    "CapabilityStorageUniformBufferBlock16";
  case spv::Capability::CapabilityStorageUniform16:
    return "CapabilityStorageUniform16";
    //    case spv::Capability::CapabilityUniformAndStorageBuffer16BitAccess :
    //    return "CapabilityUniformAndStorageBuffer16BitAccess";
  case spv::Capability::CapabilityStoragePushConstant16:
    return "CapabilityStoragePushConstant16";
  case spv::Capability::CapabilityStorageInputOutput16:
    return "CapabilityStorageInputOutput16";
  case spv::Capability::CapabilityDeviceGroup:
    return "CapabilityDeviceGroup";
  case spv::Capability::CapabilityMultiView:
    return "CapabilityMultiView";
  case spv::Capability::CapabilityVariablePointersStorageBuffer:
    return "CapabilityVariablePointersStorageBuffer";
  case spv::Capability::CapabilityVariablePointers:
    return "CapabilityVariablePointers";
  case spv::Capability::CapabilityAtomicStorageOps:
    return "CapabilityAtomicStorageOps";
  case spv::Capability::CapabilitySampleMaskPostDepthCoverage:
    return "CapabilitySampleMaskPostDepthCoverage";
  case spv::Capability::CapabilityStorageBuffer8BitAccess:
    return "CapabilityStorageBuffer8BitAccess";
  case spv::Capability::CapabilityUniformAndStorageBuffer8BitAccess:
    return "CapabilityUniformAndStorageBuffer8BitAccess";
  case spv::Capability::CapabilityStoragePushConstant8:
    return "CapabilityStoragePushConstant8";
  case spv::Capability::CapabilityDenormPreserve:
    return "CapabilityDenormPreserve";
  case spv::Capability::CapabilityDenormFlushToZero:
    return "CapabilityDenormFlushToZero";
  case spv::Capability::CapabilitySignedZeroInfNanPreserve:
    return "CapabilitySignedZeroInfNanPreserve";
  case spv::Capability::CapabilityRoundingModeRTE:
    return "CapabilityRoundingModeRTE";
  case spv::Capability::CapabilityRoundingModeRTZ:
    return "CapabilityRoundingModeRTZ";
  case spv::Capability::CapabilityFloat16ImageAMD:
    return "CapabilityFloat16ImageAMD";
  case spv::Capability::CapabilityImageGatherBiasLodAMD:
    return "CapabilityImageGatherBiasLodAMD";
  case spv::Capability::CapabilityFragmentMaskAMD:
    return "CapabilityFragmentMaskAMD";
  case spv::Capability::CapabilityStencilExportEXT:
    return "CapabilityStencilExportEXT";
  case spv::Capability::CapabilityImageReadWriteLodAMD:
    return "CapabilityImageReadWriteLodAMD";
  case spv::Capability::CapabilityShaderClockKHR:
    return "CapabilityShaderClockKHR";
  case spv::Capability::CapabilitySampleMaskOverrideCoverageNV:
    return "CapabilitySampleMaskOverrideCoverageNV";
  case spv::Capability::CapabilityGeometryShaderPassthroughNV:
    return "CapabilityGeometryShaderPassthroughNV";
  case spv::Capability::CapabilityShaderViewportIndexLayerEXT:
    return "CapabilityShaderViewportIndexLayerEXT";
    //    case spv::Capability::CapabilityShaderViewportIndexLayerNV : return
    //    "CapabilityShaderViewportIndexLayerNV";
  case spv::Capability::CapabilityShaderViewportMaskNV:
    return "CapabilityShaderViewportMaskNV";
  case spv::Capability::CapabilityShaderStereoViewNV:
    return "CapabilityShaderStereoViewNV";
  case spv::Capability::CapabilityPerViewAttributesNV:
    return "CapabilityPerViewAttributesNV";
  case spv::Capability::CapabilityFragmentFullyCoveredEXT:
    return "CapabilityFragmentFullyCoveredEXT";
  case spv::Capability::CapabilityMeshShadingNV:
    return "CapabilityMeshShadingNV";
  case spv::Capability::CapabilityImageFootprintNV:
    return "CapabilityImageFootprintNV";
  case spv::Capability::CapabilityFragmentBarycentricNV:
    return "CapabilityFragmentBarycentricNV";
  case spv::Capability::CapabilityComputeDerivativeGroupQuadsNV:
    return "CapabilityComputeDerivativeGroupQuadsNV";
  case spv::Capability::CapabilityFragmentDensityEXT:
    return "CapabilityFragmentDensityEXT";
    //    case spv::Capability::CapabilityShadingRateNV : return
    //    "CapabilityShadingRateNV";
  case spv::Capability::CapabilityGroupNonUniformPartitionedNV:
    return "CapabilityGroupNonUniformPartitionedNV";
  case spv::Capability::CapabilityShaderNonUniform:
    return "CapabilityShaderNonUniform";
    //    case spv::Capability::CapabilityShaderNonUniformEXT : return
    //    "CapabilityShaderNonUniformEXT";
  case spv::Capability::CapabilityRuntimeDescriptorArray:
    return "CapabilityRuntimeDescriptorArray";
    //    case spv::Capability::CapabilityRuntimeDescriptorArrayEXT : return
    //    "CapabilityRuntimeDescriptorArrayEXT";
  case spv::Capability::CapabilityInputAttachmentArrayDynamicIndexing:
    return "CapabilityInputAttachmentArrayDynamicIndexing";
    //    case spv::Capability::CapabilityInputAttachmentArrayDynamicIndexingEXT
    //    : return "CapabilityInputAttachmentArrayDynamicIndexingEXT";
  case spv::Capability::CapabilityUniformTexelBufferArrayDynamicIndexing:
    return "CapabilityUniformTexelBufferArrayDynamicIndexing";
    //    case
    //    spv::Capability::CapabilityUniformTexelBufferArrayDynamicIndexingEXT :
    //    return "CapabilityUniformTexelBufferArrayDynamicIndexingEXT";
  case spv::Capability::CapabilityStorageTexelBufferArrayDynamicIndexing:
    return "CapabilityStorageTexelBufferArrayDynamicIndexing";
    //    case
    //    spv::Capability::CapabilityStorageTexelBufferArrayDynamicIndexingEXT :
    //    return "CapabilityStorageTexelBufferArrayDynamicIndexingEXT";
  case spv::Capability::CapabilityUniformBufferArrayNonUniformIndexing:
    return "CapabilityUniformBufferArrayNonUniformIndexing";
    //    case
    //    spv::Capability::CapabilityUniformBufferArrayNonUniformIndexingEXT :
    //    return "CapabilityUniformBufferArrayNonUniformIndexingEXT";
  case spv::Capability::CapabilitySampledImageArrayNonUniformIndexing:
    return "CapabilitySampledImageArrayNonUniformIndexing";
    //    case spv::Capability::CapabilitySampledImageArrayNonUniformIndexingEXT
    //    : return "CapabilitySampledImageArrayNonUniformIndexingEXT";
  case spv::Capability::CapabilityStorageBufferArrayNonUniformIndexing:
    return "CapabilityStorageBufferArrayNonUniformIndexing";
    //    case
    //    spv::Capability::CapabilityStorageBufferArrayNonUniformIndexingEXT :
    //    return "CapabilityStorageBufferArrayNonUniformIndexingEXT";
  case spv::Capability::CapabilityStorageImageArrayNonUniformIndexing:
    return "CapabilityStorageImageArrayNonUniformIndexing";
    //    case spv::Capability::CapabilityStorageImageArrayNonUniformIndexingEXT
    //    : return "CapabilityStorageImageArrayNonUniformIndexingEXT";
  case spv::Capability::CapabilityInputAttachmentArrayNonUniformIndexing:
    return "CapabilityInputAttachmentArrayNonUniformIndexing";
    //    case
    //    spv::Capability::CapabilityInputAttachmentArrayNonUniformIndexingEXT :
    //    return "CapabilityInputAttachmentArrayNonUniformIndexingEXT";
  case spv::Capability::CapabilityUniformTexelBufferArrayNonUniformIndexing:
    return "CapabilityUniformTexelBufferArrayNonUniformIndexing";
    //    case
    //    spv::Capability::CapabilityUniformTexelBufferArrayNonUniformIndexingEXT
    //    : return "CapabilityUniformTexelBufferArrayNonUniformIndexingEXT";
  case spv::Capability::CapabilityStorageTexelBufferArrayNonUniformIndexing:
    return "CapabilityStorageTexelBufferArrayNonUniformIndexing";
    //    case
    //    spv::Capability::CapabilityStorageTexelBufferArrayNonUniformIndexingEXT
    //    : return "CapabilityStorageTexelBufferArrayNonUniformIndexingEXT";
  case spv::Capability::CapabilityRayTracingNV:
    return "CapabilityRayTracingNV";
  case spv::Capability::CapabilityVulkanMemoryModel:
    return "CapabilityVulkanMemoryModel";
    //    case spv::Capability::CapabilityVulkanMemoryModelKHR : return
    //    "CapabilityVulkanMemoryModelKHR";
  case spv::Capability::CapabilityVulkanMemoryModelDeviceScope:
    return "CapabilityVulkanMemoryModelDeviceScope";
    //    case spv::Capability::CapabilityVulkanMemoryModelDeviceScopeKHR :
    //    return "CapabilityVulkanMemoryModelDeviceScopeKHR";
  case spv::Capability::CapabilityPhysicalStorageBufferAddresses:
    return "CapabilityPhysicalStorageBufferAddresses";
    //    case spv::Capability::CapabilityPhysicalStorageBufferAddressesEXT :
    //    return "CapabilityPhysicalStorageBufferAddressesEXT";
  case spv::Capability::CapabilityComputeDerivativeGroupLinearNV:
    return "CapabilityComputeDerivativeGroupLinearNV";
  case spv::Capability::CapabilityCooperativeMatrixNV:
    return "CapabilityCooperativeMatrixNV";
  case spv::Capability::CapabilityFragmentShaderSampleInterlockEXT:
    return "CapabilityFragmentShaderSampleInterlockEXT";
  case spv::Capability::CapabilityFragmentShaderShadingRateInterlockEXT:
    return "CapabilityFragmentShaderShadingRateInterlockEXT";
  case spv::Capability::CapabilityShaderSMBuiltinsNV:
    return "CapabilityShaderSMBuiltinsNV";
  case spv::Capability::CapabilityFragmentShaderPixelInterlockEXT:
    return "CapabilityFragmentShaderPixelInterlockEXT";
  case spv::Capability::CapabilityDemoteToHelperInvocationEXT:
    return "CapabilityDemoteToHelperInvocationEXT";
  case spv::Capability::CapabilitySubgroupShuffleINTEL:
    return "CapabilitySubgroupShuffleINTEL";
  case spv::Capability::CapabilitySubgroupBufferBlockIOINTEL:
    return "CapabilitySubgroupBufferBlockIOINTEL";
  case spv::Capability::CapabilitySubgroupImageBlockIOINTEL:
    return "CapabilitySubgroupImageBlockIOINTEL";
  case spv::Capability::CapabilitySubgroupImageMediaBlockIOINTEL:
    return "CapabilitySubgroupImageMediaBlockIOINTEL";
  case spv::Capability::CapabilityIntegerFunctions2INTEL:
    return "CapabilityIntegerFunctions2INTEL";
  case spv::Capability::CapabilitySubgroupAvcMotionEstimationINTEL:
    return "CapabilitySubgroupAvcMotionEstimationINTEL";
  case spv::Capability::CapabilitySubgroupAvcMotionEstimationIntraINTEL:
    return "CapabilitySubgroupAvcMotionEstimationIntraINTEL";
  case spv::Capability::CapabilitySubgroupAvcMotionEstimationChromaINTEL:
    return "CapabilitySubgroupAvcMotionEstimationChromaINTEL";
  case spv::Capability::CapabilityMax:
    return "CapabilityMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::Capability const &str) { dump(get_cstr(str)); }
char const *get_cstr(spv::Op const &str) {
  switch (str) {
  case spv::Op::OpNop:
    return "OpNop";
  case spv::Op::OpUndef:
    return "OpUndef";
  case spv::Op::OpSourceContinued:
    return "OpSourceContinued";
  case spv::Op::OpSource:
    return "OpSource";
  case spv::Op::OpSourceExtension:
    return "OpSourceExtension";
  case spv::Op::OpName:
    return "OpName";
  case spv::Op::OpMemberName:
    return "OpMemberName";
  case spv::Op::OpString:
    return "OpString";
  case spv::Op::OpLine:
    return "OpLine";
  case spv::Op::OpExtension:
    return "OpExtension";
  case spv::Op::OpExtInstImport:
    return "OpExtInstImport";
  case spv::Op::OpExtInst:
    return "OpExtInst";
  case spv::Op::OpMemoryModel:
    return "OpMemoryModel";
  case spv::Op::OpEntryPoint:
    return "OpEntryPoint";
  case spv::Op::OpExecutionMode:
    return "OpExecutionMode";
  case spv::Op::OpCapability:
    return "OpCapability";
  case spv::Op::OpTypeVoid:
    return "OpTypeVoid";
  case spv::Op::OpTypeBool:
    return "OpTypeBool";
  case spv::Op::OpTypeInt:
    return "OpTypeInt";
  case spv::Op::OpTypeFloat:
    return "OpTypeFloat";
  case spv::Op::OpTypeVector:
    return "OpTypeVector";
  case spv::Op::OpTypeMatrix:
    return "OpTypeMatrix";
  case spv::Op::OpTypeImage:
    return "OpTypeImage";
  case spv::Op::OpTypeSampler:
    return "OpTypeSampler";
  case spv::Op::OpTypeSampledImage:
    return "OpTypeSampledImage";
  case spv::Op::OpTypeArray:
    return "OpTypeArray";
  case spv::Op::OpTypeRuntimeArray:
    return "OpTypeRuntimeArray";
  case spv::Op::OpTypeStruct:
    return "OpTypeStruct";
  case spv::Op::OpTypeOpaque:
    return "OpTypeOpaque";
  case spv::Op::OpTypePointer:
    return "OpTypePointer";
  case spv::Op::OpTypeFunction:
    return "OpTypeFunction";
  case spv::Op::OpTypeEvent:
    return "OpTypeEvent";
  case spv::Op::OpTypeDeviceEvent:
    return "OpTypeDeviceEvent";
  case spv::Op::OpTypeReserveId:
    return "OpTypeReserveId";
  case spv::Op::OpTypeQueue:
    return "OpTypeQueue";
  case spv::Op::OpTypePipe:
    return "OpTypePipe";
  case spv::Op::OpTypeForwardPointer:
    return "OpTypeForwardPointer";
  case spv::Op::OpConstantTrue:
    return "OpConstantTrue";
  case spv::Op::OpConstantFalse:
    return "OpConstantFalse";
  case spv::Op::OpConstant:
    return "OpConstant";
  case spv::Op::OpConstantComposite:
    return "OpConstantComposite";
  case spv::Op::OpConstantSampler:
    return "OpConstantSampler";
  case spv::Op::OpConstantNull:
    return "OpConstantNull";
  case spv::Op::OpSpecConstantTrue:
    return "OpSpecConstantTrue";
  case spv::Op::OpSpecConstantFalse:
    return "OpSpecConstantFalse";
  case spv::Op::OpSpecConstant:
    return "OpSpecConstant";
  case spv::Op::OpSpecConstantComposite:
    return "OpSpecConstantComposite";
  case spv::Op::OpSpecConstantOp:
    return "OpSpecConstantOp";
  case spv::Op::OpFunction:
    return "OpFunction";
  case spv::Op::OpFunctionParameter:
    return "OpFunctionParameter";
  case spv::Op::OpFunctionEnd:
    return "OpFunctionEnd";
  case spv::Op::OpFunctionCall:
    return "OpFunctionCall";
  case spv::Op::OpVariable:
    return "OpVariable";
  case spv::Op::OpImageTexelPointer:
    return "OpImageTexelPointer";
  case spv::Op::OpLoad:
    return "OpLoad";
  case spv::Op::OpStore:
    return "OpStore";
  case spv::Op::OpCopyMemory:
    return "OpCopyMemory";
  case spv::Op::OpCopyMemorySized:
    return "OpCopyMemorySized";
  case spv::Op::OpAccessChain:
    return "OpAccessChain";
  case spv::Op::OpInBoundsAccessChain:
    return "OpInBoundsAccessChain";
  case spv::Op::OpPtrAccessChain:
    return "OpPtrAccessChain";
  case spv::Op::OpArrayLength:
    return "OpArrayLength";
  case spv::Op::OpGenericPtrMemSemantics:
    return "OpGenericPtrMemSemantics";
  case spv::Op::OpInBoundsPtrAccessChain:
    return "OpInBoundsPtrAccessChain";
  case spv::Op::OpDecorate:
    return "OpDecorate";
  case spv::Op::OpMemberDecorate:
    return "OpMemberDecorate";
  case spv::Op::OpDecorationGroup:
    return "OpDecorationGroup";
  case spv::Op::OpGroupDecorate:
    return "OpGroupDecorate";
  case spv::Op::OpGroupMemberDecorate:
    return "OpGroupMemberDecorate";
  case spv::Op::OpVectorExtractDynamic:
    return "OpVectorExtractDynamic";
  case spv::Op::OpVectorInsertDynamic:
    return "OpVectorInsertDynamic";
  case spv::Op::OpVectorShuffle:
    return "OpVectorShuffle";
  case spv::Op::OpCompositeConstruct:
    return "OpCompositeConstruct";
  case spv::Op::OpCompositeExtract:
    return "OpCompositeExtract";
  case spv::Op::OpCompositeInsert:
    return "OpCompositeInsert";
  case spv::Op::OpCopyObject:
    return "OpCopyObject";
  case spv::Op::OpTranspose:
    return "OpTranspose";
  case spv::Op::OpSampledImage:
    return "OpSampledImage";
  case spv::Op::OpImageSampleImplicitLod:
    return "OpImageSampleImplicitLod";
  case spv::Op::OpImageSampleExplicitLod:
    return "OpImageSampleExplicitLod";
  case spv::Op::OpImageSampleDrefImplicitLod:
    return "OpImageSampleDrefImplicitLod";
  case spv::Op::OpImageSampleDrefExplicitLod:
    return "OpImageSampleDrefExplicitLod";
  case spv::Op::OpImageSampleProjImplicitLod:
    return "OpImageSampleProjImplicitLod";
  case spv::Op::OpImageSampleProjExplicitLod:
    return "OpImageSampleProjExplicitLod";
  case spv::Op::OpImageSampleProjDrefImplicitLod:
    return "OpImageSampleProjDrefImplicitLod";
  case spv::Op::OpImageSampleProjDrefExplicitLod:
    return "OpImageSampleProjDrefExplicitLod";
  case spv::Op::OpImageFetch:
    return "OpImageFetch";
  case spv::Op::OpImageGather:
    return "OpImageGather";
  case spv::Op::OpImageDrefGather:
    return "OpImageDrefGather";
  case spv::Op::OpImageRead:
    return "OpImageRead";
  case spv::Op::OpImageWrite:
    return "OpImageWrite";
  case spv::Op::OpImage:
    return "OpImage";
  case spv::Op::OpImageQueryFormat:
    return "OpImageQueryFormat";
  case spv::Op::OpImageQueryOrder:
    return "OpImageQueryOrder";
  case spv::Op::OpImageQuerySizeLod:
    return "OpImageQuerySizeLod";
  case spv::Op::OpImageQuerySize:
    return "OpImageQuerySize";
  case spv::Op::OpImageQueryLod:
    return "OpImageQueryLod";
  case spv::Op::OpImageQueryLevels:
    return "OpImageQueryLevels";
  case spv::Op::OpImageQuerySamples:
    return "OpImageQuerySamples";
  case spv::Op::OpConvertFToU:
    return "OpConvertFToU";
  case spv::Op::OpConvertFToS:
    return "OpConvertFToS";
  case spv::Op::OpConvertSToF:
    return "OpConvertSToF";
  case spv::Op::OpConvertUToF:
    return "OpConvertUToF";
  case spv::Op::OpUConvert:
    return "OpUConvert";
  case spv::Op::OpSConvert:
    return "OpSConvert";
  case spv::Op::OpFConvert:
    return "OpFConvert";
  case spv::Op::OpQuantizeToF16:
    return "OpQuantizeToF16";
  case spv::Op::OpConvertPtrToU:
    return "OpConvertPtrToU";
  case spv::Op::OpSatConvertSToU:
    return "OpSatConvertSToU";
  case spv::Op::OpSatConvertUToS:
    return "OpSatConvertUToS";
  case spv::Op::OpConvertUToPtr:
    return "OpConvertUToPtr";
  case spv::Op::OpPtrCastToGeneric:
    return "OpPtrCastToGeneric";
  case spv::Op::OpGenericCastToPtr:
    return "OpGenericCastToPtr";
  case spv::Op::OpGenericCastToPtrExplicit:
    return "OpGenericCastToPtrExplicit";
  case spv::Op::OpBitcast:
    return "OpBitcast";
  case spv::Op::OpSNegate:
    return "OpSNegate";
  case spv::Op::OpFNegate:
    return "OpFNegate";
  case spv::Op::OpIAdd:
    return "OpIAdd";
  case spv::Op::OpFAdd:
    return "OpFAdd";
  case spv::Op::OpISub:
    return "OpISub";
  case spv::Op::OpFSub:
    return "OpFSub";
  case spv::Op::OpIMul:
    return "OpIMul";
  case spv::Op::OpFMul:
    return "OpFMul";
  case spv::Op::OpUDiv:
    return "OpUDiv";
  case spv::Op::OpSDiv:
    return "OpSDiv";
  case spv::Op::OpFDiv:
    return "OpFDiv";
  case spv::Op::OpUMod:
    return "OpUMod";
  case spv::Op::OpSRem:
    return "OpSRem";
  case spv::Op::OpSMod:
    return "OpSMod";
  case spv::Op::OpFRem:
    return "OpFRem";
  case spv::Op::OpFMod:
    return "OpFMod";
  case spv::Op::OpVectorTimesScalar:
    return "OpVectorTimesScalar";
  case spv::Op::OpMatrixTimesScalar:
    return "OpMatrixTimesScalar";
  case spv::Op::OpVectorTimesMatrix:
    return "OpVectorTimesMatrix";
  case spv::Op::OpMatrixTimesVector:
    return "OpMatrixTimesVector";
  case spv::Op::OpMatrixTimesMatrix:
    return "OpMatrixTimesMatrix";
  case spv::Op::OpOuterProduct:
    return "OpOuterProduct";
  case spv::Op::OpDot:
    return "OpDot";
  case spv::Op::OpIAddCarry:
    return "OpIAddCarry";
  case spv::Op::OpISubBorrow:
    return "OpISubBorrow";
  case spv::Op::OpUMulExtended:
    return "OpUMulExtended";
  case spv::Op::OpSMulExtended:
    return "OpSMulExtended";
  case spv::Op::OpAny:
    return "OpAny";
  case spv::Op::OpAll:
    return "OpAll";
  case spv::Op::OpIsNan:
    return "OpIsNan";
  case spv::Op::OpIsInf:
    return "OpIsInf";
  case spv::Op::OpIsFinite:
    return "OpIsFinite";
  case spv::Op::OpIsNormal:
    return "OpIsNormal";
  case spv::Op::OpSignBitSet:
    return "OpSignBitSet";
  case spv::Op::OpLessOrGreater:
    return "OpLessOrGreater";
  case spv::Op::OpOrdered:
    return "OpOrdered";
  case spv::Op::OpUnordered:
    return "OpUnordered";
  case spv::Op::OpLogicalEqual:
    return "OpLogicalEqual";
  case spv::Op::OpLogicalNotEqual:
    return "OpLogicalNotEqual";
  case spv::Op::OpLogicalOr:
    return "OpLogicalOr";
  case spv::Op::OpLogicalAnd:
    return "OpLogicalAnd";
  case spv::Op::OpLogicalNot:
    return "OpLogicalNot";
  case spv::Op::OpSelect:
    return "OpSelect";
  case spv::Op::OpIEqual:
    return "OpIEqual";
  case spv::Op::OpINotEqual:
    return "OpINotEqual";
  case spv::Op::OpUGreaterThan:
    return "OpUGreaterThan";
  case spv::Op::OpSGreaterThan:
    return "OpSGreaterThan";
  case spv::Op::OpUGreaterThanEqual:
    return "OpUGreaterThanEqual";
  case spv::Op::OpSGreaterThanEqual:
    return "OpSGreaterThanEqual";
  case spv::Op::OpULessThan:
    return "OpULessThan";
  case spv::Op::OpSLessThan:
    return "OpSLessThan";
  case spv::Op::OpULessThanEqual:
    return "OpULessThanEqual";
  case spv::Op::OpSLessThanEqual:
    return "OpSLessThanEqual";
  case spv::Op::OpFOrdEqual:
    return "OpFOrdEqual";
  case spv::Op::OpFUnordEqual:
    return "OpFUnordEqual";
  case spv::Op::OpFOrdNotEqual:
    return "OpFOrdNotEqual";
  case spv::Op::OpFUnordNotEqual:
    return "OpFUnordNotEqual";
  case spv::Op::OpFOrdLessThan:
    return "OpFOrdLessThan";
  case spv::Op::OpFUnordLessThan:
    return "OpFUnordLessThan";
  case spv::Op::OpFOrdGreaterThan:
    return "OpFOrdGreaterThan";
  case spv::Op::OpFUnordGreaterThan:
    return "OpFUnordGreaterThan";
  case spv::Op::OpFOrdLessThanEqual:
    return "OpFOrdLessThanEqual";
  case spv::Op::OpFUnordLessThanEqual:
    return "OpFUnordLessThanEqual";
  case spv::Op::OpFOrdGreaterThanEqual:
    return "OpFOrdGreaterThanEqual";
  case spv::Op::OpFUnordGreaterThanEqual:
    return "OpFUnordGreaterThanEqual";
  case spv::Op::OpShiftRightLogical:
    return "OpShiftRightLogical";
  case spv::Op::OpShiftRightArithmetic:
    return "OpShiftRightArithmetic";
  case spv::Op::OpShiftLeftLogical:
    return "OpShiftLeftLogical";
  case spv::Op::OpBitwiseOr:
    return "OpBitwiseOr";
  case spv::Op::OpBitwiseXor:
    return "OpBitwiseXor";
  case spv::Op::OpBitwiseAnd:
    return "OpBitwiseAnd";
  case spv::Op::OpNot:
    return "OpNot";
  case spv::Op::OpBitFieldInsert:
    return "OpBitFieldInsert";
  case spv::Op::OpBitFieldSExtract:
    return "OpBitFieldSExtract";
  case spv::Op::OpBitFieldUExtract:
    return "OpBitFieldUExtract";
  case spv::Op::OpBitReverse:
    return "OpBitReverse";
  case spv::Op::OpBitCount:
    return "OpBitCount";
  case spv::Op::OpDPdx:
    return "OpDPdx";
  case spv::Op::OpDPdy:
    return "OpDPdy";
  case spv::Op::OpFwidth:
    return "OpFwidth";
  case spv::Op::OpDPdxFine:
    return "OpDPdxFine";
  case spv::Op::OpDPdyFine:
    return "OpDPdyFine";
  case spv::Op::OpFwidthFine:
    return "OpFwidthFine";
  case spv::Op::OpDPdxCoarse:
    return "OpDPdxCoarse";
  case spv::Op::OpDPdyCoarse:
    return "OpDPdyCoarse";
  case spv::Op::OpFwidthCoarse:
    return "OpFwidthCoarse";
  case spv::Op::OpEmitVertex:
    return "OpEmitVertex";
  case spv::Op::OpEndPrimitive:
    return "OpEndPrimitive";
  case spv::Op::OpEmitStreamVertex:
    return "OpEmitStreamVertex";
  case spv::Op::OpEndStreamPrimitive:
    return "OpEndStreamPrimitive";
  case spv::Op::OpControlBarrier:
    return "OpControlBarrier";
  case spv::Op::OpMemoryBarrier:
    return "OpMemoryBarrier";
  case spv::Op::OpAtomicLoad:
    return "OpAtomicLoad";
  case spv::Op::OpAtomicStore:
    return "OpAtomicStore";
  case spv::Op::OpAtomicExchange:
    return "OpAtomicExchange";
  case spv::Op::OpAtomicCompareExchange:
    return "OpAtomicCompareExchange";
  case spv::Op::OpAtomicCompareExchangeWeak:
    return "OpAtomicCompareExchangeWeak";
  case spv::Op::OpAtomicIIncrement:
    return "OpAtomicIIncrement";
  case spv::Op::OpAtomicIDecrement:
    return "OpAtomicIDecrement";
  case spv::Op::OpAtomicIAdd:
    return "OpAtomicIAdd";
  case spv::Op::OpAtomicISub:
    return "OpAtomicISub";
  case spv::Op::OpAtomicSMin:
    return "OpAtomicSMin";
  case spv::Op::OpAtomicUMin:
    return "OpAtomicUMin";
  case spv::Op::OpAtomicSMax:
    return "OpAtomicSMax";
  case spv::Op::OpAtomicUMax:
    return "OpAtomicUMax";
  case spv::Op::OpAtomicAnd:
    return "OpAtomicAnd";
  case spv::Op::OpAtomicOr:
    return "OpAtomicOr";
  case spv::Op::OpAtomicXor:
    return "OpAtomicXor";
  case spv::Op::OpPhi:
    return "OpPhi";
  case spv::Op::OpLoopMerge:
    return "OpLoopMerge";
  case spv::Op::OpSelectionMerge:
    return "OpSelectionMerge";
  case spv::Op::OpLabel:
    return "OpLabel";
  case spv::Op::OpBranch:
    return "OpBranch";
  case spv::Op::OpBranchConditional:
    return "OpBranchConditional";
  case spv::Op::OpSwitch:
    return "OpSwitch";
  case spv::Op::OpKill:
    return "OpKill";
  case spv::Op::OpReturn:
    return "OpReturn";
  case spv::Op::OpReturnValue:
    return "OpReturnValue";
  case spv::Op::OpUnreachable:
    return "OpUnreachable";
  case spv::Op::OpLifetimeStart:
    return "OpLifetimeStart";
  case spv::Op::OpLifetimeStop:
    return "OpLifetimeStop";
  case spv::Op::OpGroupAsyncCopy:
    return "OpGroupAsyncCopy";
  case spv::Op::OpGroupWaitEvents:
    return "OpGroupWaitEvents";
  case spv::Op::OpGroupAll:
    return "OpGroupAll";
  case spv::Op::OpGroupAny:
    return "OpGroupAny";
  case spv::Op::OpGroupBroadcast:
    return "OpGroupBroadcast";
  case spv::Op::OpGroupIAdd:
    return "OpGroupIAdd";
  case spv::Op::OpGroupFAdd:
    return "OpGroupFAdd";
  case spv::Op::OpGroupFMin:
    return "OpGroupFMin";
  case spv::Op::OpGroupUMin:
    return "OpGroupUMin";
  case spv::Op::OpGroupSMin:
    return "OpGroupSMin";
  case spv::Op::OpGroupFMax:
    return "OpGroupFMax";
  case spv::Op::OpGroupUMax:
    return "OpGroupUMax";
  case spv::Op::OpGroupSMax:
    return "OpGroupSMax";
  case spv::Op::OpReadPipe:
    return "OpReadPipe";
  case spv::Op::OpWritePipe:
    return "OpWritePipe";
  case spv::Op::OpReservedReadPipe:
    return "OpReservedReadPipe";
  case spv::Op::OpReservedWritePipe:
    return "OpReservedWritePipe";
  case spv::Op::OpReserveReadPipePackets:
    return "OpReserveReadPipePackets";
  case spv::Op::OpReserveWritePipePackets:
    return "OpReserveWritePipePackets";
  case spv::Op::OpCommitReadPipe:
    return "OpCommitReadPipe";
  case spv::Op::OpCommitWritePipe:
    return "OpCommitWritePipe";
  case spv::Op::OpIsValidReserveId:
    return "OpIsValidReserveId";
  case spv::Op::OpGetNumPipePackets:
    return "OpGetNumPipePackets";
  case spv::Op::OpGetMaxPipePackets:
    return "OpGetMaxPipePackets";
  case spv::Op::OpGroupReserveReadPipePackets:
    return "OpGroupReserveReadPipePackets";
  case spv::Op::OpGroupReserveWritePipePackets:
    return "OpGroupReserveWritePipePackets";
  case spv::Op::OpGroupCommitReadPipe:
    return "OpGroupCommitReadPipe";
  case spv::Op::OpGroupCommitWritePipe:
    return "OpGroupCommitWritePipe";
  case spv::Op::OpEnqueueMarker:
    return "OpEnqueueMarker";
  case spv::Op::OpEnqueueKernel:
    return "OpEnqueueKernel";
  case spv::Op::OpGetKernelNDrangeSubGroupCount:
    return "OpGetKernelNDrangeSubGroupCount";
  case spv::Op::OpGetKernelNDrangeMaxSubGroupSize:
    return "OpGetKernelNDrangeMaxSubGroupSize";
  case spv::Op::OpGetKernelWorkGroupSize:
    return "OpGetKernelWorkGroupSize";
  case spv::Op::OpGetKernelPreferredWorkGroupSizeMultiple:
    return "OpGetKernelPreferredWorkGroupSizeMultiple";
  case spv::Op::OpRetainEvent:
    return "OpRetainEvent";
  case spv::Op::OpReleaseEvent:
    return "OpReleaseEvent";
  case spv::Op::OpCreateUserEvent:
    return "OpCreateUserEvent";
  case spv::Op::OpIsValidEvent:
    return "OpIsValidEvent";
  case spv::Op::OpSetUserEventStatus:
    return "OpSetUserEventStatus";
  case spv::Op::OpCaptureEventProfilingInfo:
    return "OpCaptureEventProfilingInfo";
  case spv::Op::OpGetDefaultQueue:
    return "OpGetDefaultQueue";
  case spv::Op::OpBuildNDRange:
    return "OpBuildNDRange";
  case spv::Op::OpImageSparseSampleImplicitLod:
    return "OpImageSparseSampleImplicitLod";
  case spv::Op::OpImageSparseSampleExplicitLod:
    return "OpImageSparseSampleExplicitLod";
  case spv::Op::OpImageSparseSampleDrefImplicitLod:
    return "OpImageSparseSampleDrefImplicitLod";
  case spv::Op::OpImageSparseSampleDrefExplicitLod:
    return "OpImageSparseSampleDrefExplicitLod";
  case spv::Op::OpImageSparseSampleProjImplicitLod:
    return "OpImageSparseSampleProjImplicitLod";
  case spv::Op::OpImageSparseSampleProjExplicitLod:
    return "OpImageSparseSampleProjExplicitLod";
  case spv::Op::OpImageSparseSampleProjDrefImplicitLod:
    return "OpImageSparseSampleProjDrefImplicitLod";
  case spv::Op::OpImageSparseSampleProjDrefExplicitLod:
    return "OpImageSparseSampleProjDrefExplicitLod";
  case spv::Op::OpImageSparseFetch:
    return "OpImageSparseFetch";
  case spv::Op::OpImageSparseGather:
    return "OpImageSparseGather";
  case spv::Op::OpImageSparseDrefGather:
    return "OpImageSparseDrefGather";
  case spv::Op::OpImageSparseTexelsResident:
    return "OpImageSparseTexelsResident";
  case spv::Op::OpNoLine:
    return "OpNoLine";
  case spv::Op::OpAtomicFlagTestAndSet:
    return "OpAtomicFlagTestAndSet";
  case spv::Op::OpAtomicFlagClear:
    return "OpAtomicFlagClear";
  case spv::Op::OpImageSparseRead:
    return "OpImageSparseRead";
  case spv::Op::OpSizeOf:
    return "OpSizeOf";
  case spv::Op::OpTypePipeStorage:
    return "OpTypePipeStorage";
  case spv::Op::OpConstantPipeStorage:
    return "OpConstantPipeStorage";
  case spv::Op::OpCreatePipeFromPipeStorage:
    return "OpCreatePipeFromPipeStorage";
  case spv::Op::OpGetKernelLocalSizeForSubgroupCount:
    return "OpGetKernelLocalSizeForSubgroupCount";
  case spv::Op::OpGetKernelMaxNumSubgroups:
    return "OpGetKernelMaxNumSubgroups";
  case spv::Op::OpTypeNamedBarrier:
    return "OpTypeNamedBarrier";
  case spv::Op::OpNamedBarrierInitialize:
    return "OpNamedBarrierInitialize";
  case spv::Op::OpMemoryNamedBarrier:
    return "OpMemoryNamedBarrier";
  case spv::Op::OpModuleProcessed:
    return "OpModuleProcessed";
  case spv::Op::OpExecutionModeId:
    return "OpExecutionModeId";
  case spv::Op::OpDecorateId:
    return "OpDecorateId";
  case spv::Op::OpGroupNonUniformElect:
    return "OpGroupNonUniformElect";
  case spv::Op::OpGroupNonUniformAll:
    return "OpGroupNonUniformAll";
  case spv::Op::OpGroupNonUniformAny:
    return "OpGroupNonUniformAny";
  case spv::Op::OpGroupNonUniformAllEqual:
    return "OpGroupNonUniformAllEqual";
  case spv::Op::OpGroupNonUniformBroadcast:
    return "OpGroupNonUniformBroadcast";
  case spv::Op::OpGroupNonUniformBroadcastFirst:
    return "OpGroupNonUniformBroadcastFirst";
  case spv::Op::OpGroupNonUniformBallot:
    return "OpGroupNonUniformBallot";
  case spv::Op::OpGroupNonUniformInverseBallot:
    return "OpGroupNonUniformInverseBallot";
  case spv::Op::OpGroupNonUniformBallotBitExtract:
    return "OpGroupNonUniformBallotBitExtract";
  case spv::Op::OpGroupNonUniformBallotBitCount:
    return "OpGroupNonUniformBallotBitCount";
  case spv::Op::OpGroupNonUniformBallotFindLSB:
    return "OpGroupNonUniformBallotFindLSB";
  case spv::Op::OpGroupNonUniformBallotFindMSB:
    return "OpGroupNonUniformBallotFindMSB";
  case spv::Op::OpGroupNonUniformShuffle:
    return "OpGroupNonUniformShuffle";
  case spv::Op::OpGroupNonUniformShuffleXor:
    return "OpGroupNonUniformShuffleXor";
  case spv::Op::OpGroupNonUniformShuffleUp:
    return "OpGroupNonUniformShuffleUp";
  case spv::Op::OpGroupNonUniformShuffleDown:
    return "OpGroupNonUniformShuffleDown";
  case spv::Op::OpGroupNonUniformIAdd:
    return "OpGroupNonUniformIAdd";
  case spv::Op::OpGroupNonUniformFAdd:
    return "OpGroupNonUniformFAdd";
  case spv::Op::OpGroupNonUniformIMul:
    return "OpGroupNonUniformIMul";
  case spv::Op::OpGroupNonUniformFMul:
    return "OpGroupNonUniformFMul";
  case spv::Op::OpGroupNonUniformSMin:
    return "OpGroupNonUniformSMin";
  case spv::Op::OpGroupNonUniformUMin:
    return "OpGroupNonUniformUMin";
  case spv::Op::OpGroupNonUniformFMin:
    return "OpGroupNonUniformFMin";
  case spv::Op::OpGroupNonUniformSMax:
    return "OpGroupNonUniformSMax";
  case spv::Op::OpGroupNonUniformUMax:
    return "OpGroupNonUniformUMax";
  case spv::Op::OpGroupNonUniformFMax:
    return "OpGroupNonUniformFMax";
  case spv::Op::OpGroupNonUniformBitwiseAnd:
    return "OpGroupNonUniformBitwiseAnd";
  case spv::Op::OpGroupNonUniformBitwiseOr:
    return "OpGroupNonUniformBitwiseOr";
  case spv::Op::OpGroupNonUniformBitwiseXor:
    return "OpGroupNonUniformBitwiseXor";
  case spv::Op::OpGroupNonUniformLogicalAnd:
    return "OpGroupNonUniformLogicalAnd";
  case spv::Op::OpGroupNonUniformLogicalOr:
    return "OpGroupNonUniformLogicalOr";
  case spv::Op::OpGroupNonUniformLogicalXor:
    return "OpGroupNonUniformLogicalXor";
  case spv::Op::OpGroupNonUniformQuadBroadcast:
    return "OpGroupNonUniformQuadBroadcast";
  case spv::Op::OpGroupNonUniformQuadSwap:
    return "OpGroupNonUniformQuadSwap";
  case spv::Op::OpCopyLogical:
    return "OpCopyLogical";
  case spv::Op::OpPtrEqual:
    return "OpPtrEqual";
  case spv::Op::OpPtrNotEqual:
    return "OpPtrNotEqual";
  case spv::Op::OpPtrDiff:
    return "OpPtrDiff";
  case spv::Op::OpSubgroupBallotKHR:
    return "OpSubgroupBallotKHR";
  case spv::Op::OpSubgroupFirstInvocationKHR:
    return "OpSubgroupFirstInvocationKHR";
  case spv::Op::OpSubgroupAllKHR:
    return "OpSubgroupAllKHR";
  case spv::Op::OpSubgroupAnyKHR:
    return "OpSubgroupAnyKHR";
  case spv::Op::OpSubgroupAllEqualKHR:
    return "OpSubgroupAllEqualKHR";
  case spv::Op::OpSubgroupReadInvocationKHR:
    return "OpSubgroupReadInvocationKHR";
  case spv::Op::OpGroupIAddNonUniformAMD:
    return "OpGroupIAddNonUniformAMD";
  case spv::Op::OpGroupFAddNonUniformAMD:
    return "OpGroupFAddNonUniformAMD";
  case spv::Op::OpGroupFMinNonUniformAMD:
    return "OpGroupFMinNonUniformAMD";
  case spv::Op::OpGroupUMinNonUniformAMD:
    return "OpGroupUMinNonUniformAMD";
  case spv::Op::OpGroupSMinNonUniformAMD:
    return "OpGroupSMinNonUniformAMD";
  case spv::Op::OpGroupFMaxNonUniformAMD:
    return "OpGroupFMaxNonUniformAMD";
  case spv::Op::OpGroupUMaxNonUniformAMD:
    return "OpGroupUMaxNonUniformAMD";
  case spv::Op::OpGroupSMaxNonUniformAMD:
    return "OpGroupSMaxNonUniformAMD";
  case spv::Op::OpFragmentMaskFetchAMD:
    return "OpFragmentMaskFetchAMD";
  case spv::Op::OpFragmentFetchAMD:
    return "OpFragmentFetchAMD";
  case spv::Op::OpReadClockKHR:
    return "OpReadClockKHR";
  case spv::Op::OpImageSampleFootprintNV:
    return "OpImageSampleFootprintNV";
  case spv::Op::OpGroupNonUniformPartitionNV:
    return "OpGroupNonUniformPartitionNV";
  case spv::Op::OpWritePackedPrimitiveIndices4x8NV:
    return "OpWritePackedPrimitiveIndices4x8NV";
  case spv::Op::OpReportIntersectionNV:
    return "OpReportIntersectionNV";
  case spv::Op::OpIgnoreIntersectionNV:
    return "OpIgnoreIntersectionNV";
  case spv::Op::OpTerminateRayNV:
    return "OpTerminateRayNV";
  case spv::Op::OpTraceNV:
    return "OpTraceNV";
  case spv::Op::OpTypeAccelerationStructureNV:
    return "OpTypeAccelerationStructureNV";
  case spv::Op::OpExecuteCallableNV:
    return "OpExecuteCallableNV";
  case spv::Op::OpTypeCooperativeMatrixNV:
    return "OpTypeCooperativeMatrixNV";
  case spv::Op::OpCooperativeMatrixLoadNV:
    return "OpCooperativeMatrixLoadNV";
  case spv::Op::OpCooperativeMatrixStoreNV:
    return "OpCooperativeMatrixStoreNV";
  case spv::Op::OpCooperativeMatrixMulAddNV:
    return "OpCooperativeMatrixMulAddNV";
  case spv::Op::OpCooperativeMatrixLengthNV:
    return "OpCooperativeMatrixLengthNV";
  case spv::Op::OpBeginInvocationInterlockEXT:
    return "OpBeginInvocationInterlockEXT";
  case spv::Op::OpEndInvocationInterlockEXT:
    return "OpEndInvocationInterlockEXT";
  case spv::Op::OpDemoteToHelperInvocationEXT:
    return "OpDemoteToHelperInvocationEXT";
  case spv::Op::OpIsHelperInvocationEXT:
    return "OpIsHelperInvocationEXT";
  case spv::Op::OpSubgroupShuffleINTEL:
    return "OpSubgroupShuffleINTEL";
  case spv::Op::OpSubgroupShuffleDownINTEL:
    return "OpSubgroupShuffleDownINTEL";
  case spv::Op::OpSubgroupShuffleUpINTEL:
    return "OpSubgroupShuffleUpINTEL";
  case spv::Op::OpSubgroupShuffleXorINTEL:
    return "OpSubgroupShuffleXorINTEL";
  case spv::Op::OpSubgroupBlockReadINTEL:
    return "OpSubgroupBlockReadINTEL";
  case spv::Op::OpSubgroupBlockWriteINTEL:
    return "OpSubgroupBlockWriteINTEL";
  case spv::Op::OpSubgroupImageBlockReadINTEL:
    return "OpSubgroupImageBlockReadINTEL";
  case spv::Op::OpSubgroupImageBlockWriteINTEL:
    return "OpSubgroupImageBlockWriteINTEL";
  case spv::Op::OpSubgroupImageMediaBlockReadINTEL:
    return "OpSubgroupImageMediaBlockReadINTEL";
  case spv::Op::OpSubgroupImageMediaBlockWriteINTEL:
    return "OpSubgroupImageMediaBlockWriteINTEL";
  case spv::Op::OpUCountLeadingZerosINTEL:
    return "OpUCountLeadingZerosINTEL";
  case spv::Op::OpUCountTrailingZerosINTEL:
    return "OpUCountTrailingZerosINTEL";
  case spv::Op::OpAbsISubINTEL:
    return "OpAbsISubINTEL";
  case spv::Op::OpAbsUSubINTEL:
    return "OpAbsUSubINTEL";
  case spv::Op::OpIAddSatINTEL:
    return "OpIAddSatINTEL";
  case spv::Op::OpUAddSatINTEL:
    return "OpUAddSatINTEL";
  case spv::Op::OpIAverageINTEL:
    return "OpIAverageINTEL";
  case spv::Op::OpUAverageINTEL:
    return "OpUAverageINTEL";
  case spv::Op::OpIAverageRoundedINTEL:
    return "OpIAverageRoundedINTEL";
  case spv::Op::OpUAverageRoundedINTEL:
    return "OpUAverageRoundedINTEL";
  case spv::Op::OpISubSatINTEL:
    return "OpISubSatINTEL";
  case spv::Op::OpUSubSatINTEL:
    return "OpUSubSatINTEL";
  case spv::Op::OpIMul32x16INTEL:
    return "OpIMul32x16INTEL";
  case spv::Op::OpUMul32x16INTEL:
    return "OpUMul32x16INTEL";
  case spv::Op::OpDecorateString:
    return "OpDecorateString";
    //    case spv::Op::OpDecorateStringGOOGLE : return
    //    "OpDecorateStringGOOGLE";
  case spv::Op::OpMemberDecorateString:
    return "OpMemberDecorateString";
    //    case spv::Op::OpMemberDecorateStringGOOGLE : return
    //    "OpMemberDecorateStringGOOGLE";
  case spv::Op::OpVmeImageINTEL:
    return "OpVmeImageINTEL";
  case spv::Op::OpTypeVmeImageINTEL:
    return "OpTypeVmeImageINTEL";
  case spv::Op::OpTypeAvcImePayloadINTEL:
    return "OpTypeAvcImePayloadINTEL";
  case spv::Op::OpTypeAvcRefPayloadINTEL:
    return "OpTypeAvcRefPayloadINTEL";
  case spv::Op::OpTypeAvcSicPayloadINTEL:
    return "OpTypeAvcSicPayloadINTEL";
  case spv::Op::OpTypeAvcMcePayloadINTEL:
    return "OpTypeAvcMcePayloadINTEL";
  case spv::Op::OpTypeAvcMceResultINTEL:
    return "OpTypeAvcMceResultINTEL";
  case spv::Op::OpTypeAvcImeResultINTEL:
    return "OpTypeAvcImeResultINTEL";
  case spv::Op::OpTypeAvcImeResultSingleReferenceStreamoutINTEL:
    return "OpTypeAvcImeResultSingleReferenceStreamoutINTEL";
  case spv::Op::OpTypeAvcImeResultDualReferenceStreamoutINTEL:
    return "OpTypeAvcImeResultDualReferenceStreamoutINTEL";
  case spv::Op::OpTypeAvcImeSingleReferenceStreaminINTEL:
    return "OpTypeAvcImeSingleReferenceStreaminINTEL";
  case spv::Op::OpTypeAvcImeDualReferenceStreaminINTEL:
    return "OpTypeAvcImeDualReferenceStreaminINTEL";
  case spv::Op::OpTypeAvcRefResultINTEL:
    return "OpTypeAvcRefResultINTEL";
  case spv::Op::OpTypeAvcSicResultINTEL:
    return "OpTypeAvcSicResultINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultInterBaseMultiReferencePenaltyINTEL:
    return "OpSubgroupAvcMceGetDefaultInterBaseMultiReferencePenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceSetInterBaseMultiReferencePenaltyINTEL:
    return "OpSubgroupAvcMceSetInterBaseMultiReferencePenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultInterShapePenaltyINTEL:
    return "OpSubgroupAvcMceGetDefaultInterShapePenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceSetInterShapePenaltyINTEL:
    return "OpSubgroupAvcMceSetInterShapePenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultInterDirectionPenaltyINTEL:
    return "OpSubgroupAvcMceGetDefaultInterDirectionPenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceSetInterDirectionPenaltyINTEL:
    return "OpSubgroupAvcMceSetInterDirectionPenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultIntraLumaShapePenaltyINTEL:
    return "OpSubgroupAvcMceGetDefaultIntraLumaShapePenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultInterMotionVectorCostTableINTEL:
    return "OpSubgroupAvcMceGetDefaultInterMotionVectorCostTableINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultHighPenaltyCostTableINTEL:
    return "OpSubgroupAvcMceGetDefaultHighPenaltyCostTableINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultMediumPenaltyCostTableINTEL:
    return "OpSubgroupAvcMceGetDefaultMediumPenaltyCostTableINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultLowPenaltyCostTableINTEL:
    return "OpSubgroupAvcMceGetDefaultLowPenaltyCostTableINTEL";
  case spv::Op::OpSubgroupAvcMceSetMotionVectorCostFunctionINTEL:
    return "OpSubgroupAvcMceSetMotionVectorCostFunctionINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultIntraLumaModePenaltyINTEL:
    return "OpSubgroupAvcMceGetDefaultIntraLumaModePenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultNonDcLumaIntraPenaltyINTEL:
    return "OpSubgroupAvcMceGetDefaultNonDcLumaIntraPenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceGetDefaultIntraChromaModeBasePenaltyINTEL:
    return "OpSubgroupAvcMceGetDefaultIntraChromaModeBasePenaltyINTEL";
  case spv::Op::OpSubgroupAvcMceSetAcOnlyHaarINTEL:
    return "OpSubgroupAvcMceSetAcOnlyHaarINTEL";
  case spv::Op::OpSubgroupAvcMceSetSourceInterlacedFieldPolarityINTEL:
    return "OpSubgroupAvcMceSetSourceInterlacedFieldPolarityINTEL";
  case spv::Op::OpSubgroupAvcMceSetSingleReferenceInterlacedFieldPolarityINTEL:
    return "OpSubgroupAvcMceSetSingleReferenceInterlacedFieldPolarityINTEL";
  case spv::Op::OpSubgroupAvcMceSetDualReferenceInterlacedFieldPolaritiesINTEL:
    return "OpSubgroupAvcMceSetDualReferenceInterlacedFieldPolaritiesINTEL";
  case spv::Op::OpSubgroupAvcMceConvertToImePayloadINTEL:
    return "OpSubgroupAvcMceConvertToImePayloadINTEL";
  case spv::Op::OpSubgroupAvcMceConvertToImeResultINTEL:
    return "OpSubgroupAvcMceConvertToImeResultINTEL";
  case spv::Op::OpSubgroupAvcMceConvertToRefPayloadINTEL:
    return "OpSubgroupAvcMceConvertToRefPayloadINTEL";
  case spv::Op::OpSubgroupAvcMceConvertToRefResultINTEL:
    return "OpSubgroupAvcMceConvertToRefResultINTEL";
  case spv::Op::OpSubgroupAvcMceConvertToSicPayloadINTEL:
    return "OpSubgroupAvcMceConvertToSicPayloadINTEL";
  case spv::Op::OpSubgroupAvcMceConvertToSicResultINTEL:
    return "OpSubgroupAvcMceConvertToSicResultINTEL";
  case spv::Op::OpSubgroupAvcMceGetMotionVectorsINTEL:
    return "OpSubgroupAvcMceGetMotionVectorsINTEL";
  case spv::Op::OpSubgroupAvcMceGetInterDistortionsINTEL:
    return "OpSubgroupAvcMceGetInterDistortionsINTEL";
  case spv::Op::OpSubgroupAvcMceGetBestInterDistortionsINTEL:
    return "OpSubgroupAvcMceGetBestInterDistortionsINTEL";
  case spv::Op::OpSubgroupAvcMceGetInterMajorShapeINTEL:
    return "OpSubgroupAvcMceGetInterMajorShapeINTEL";
  case spv::Op::OpSubgroupAvcMceGetInterMinorShapeINTEL:
    return "OpSubgroupAvcMceGetInterMinorShapeINTEL";
  case spv::Op::OpSubgroupAvcMceGetInterDirectionsINTEL:
    return "OpSubgroupAvcMceGetInterDirectionsINTEL";
  case spv::Op::OpSubgroupAvcMceGetInterMotionVectorCountINTEL:
    return "OpSubgroupAvcMceGetInterMotionVectorCountINTEL";
  case spv::Op::OpSubgroupAvcMceGetInterReferenceIdsINTEL:
    return "OpSubgroupAvcMceGetInterReferenceIdsINTEL";
  case spv::Op::OpSubgroupAvcMceGetInterReferenceInterlacedFieldPolaritiesINTEL:
    return "OpSubgroupAvcMceGetInterReferenceInterlacedFieldPolaritiesINTEL";
  case spv::Op::OpSubgroupAvcImeInitializeINTEL:
    return "OpSubgroupAvcImeInitializeINTEL";
  case spv::Op::OpSubgroupAvcImeSetSingleReferenceINTEL:
    return "OpSubgroupAvcImeSetSingleReferenceINTEL";
  case spv::Op::OpSubgroupAvcImeSetDualReferenceINTEL:
    return "OpSubgroupAvcImeSetDualReferenceINTEL";
  case spv::Op::OpSubgroupAvcImeRefWindowSizeINTEL:
    return "OpSubgroupAvcImeRefWindowSizeINTEL";
  case spv::Op::OpSubgroupAvcImeAdjustRefOffsetINTEL:
    return "OpSubgroupAvcImeAdjustRefOffsetINTEL";
  case spv::Op::OpSubgroupAvcImeConvertToMcePayloadINTEL:
    return "OpSubgroupAvcImeConvertToMcePayloadINTEL";
  case spv::Op::OpSubgroupAvcImeSetMaxMotionVectorCountINTEL:
    return "OpSubgroupAvcImeSetMaxMotionVectorCountINTEL";
  case spv::Op::OpSubgroupAvcImeSetUnidirectionalMixDisableINTEL:
    return "OpSubgroupAvcImeSetUnidirectionalMixDisableINTEL";
  case spv::Op::OpSubgroupAvcImeSetEarlySearchTerminationThresholdINTEL:
    return "OpSubgroupAvcImeSetEarlySearchTerminationThresholdINTEL";
  case spv::Op::OpSubgroupAvcImeSetWeightedSadINTEL:
    return "OpSubgroupAvcImeSetWeightedSadINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceINTEL:
    return "OpSubgroupAvcImeEvaluateWithSingleReferenceINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceINTEL:
    return "OpSubgroupAvcImeEvaluateWithDualReferenceINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminINTEL:
    return "OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceStreaminINTEL:
    return "OpSubgroupAvcImeEvaluateWithDualReferenceStreaminINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceStreamoutINTEL:
    return "OpSubgroupAvcImeEvaluateWithSingleReferenceStreamoutINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceStreamoutINTEL:
    return "OpSubgroupAvcImeEvaluateWithDualReferenceStreamoutINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminoutINTEL:
    return "OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminoutINTEL";
  case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceStreaminoutINTEL:
    return "OpSubgroupAvcImeEvaluateWithDualReferenceStreaminoutINTEL";
  case spv::Op::OpSubgroupAvcImeConvertToMceResultINTEL:
    return "OpSubgroupAvcImeConvertToMceResultINTEL";
  case spv::Op::OpSubgroupAvcImeGetSingleReferenceStreaminINTEL:
    return "OpSubgroupAvcImeGetSingleReferenceStreaminINTEL";
  case spv::Op::OpSubgroupAvcImeGetDualReferenceStreaminINTEL:
    return "OpSubgroupAvcImeGetDualReferenceStreaminINTEL";
  case spv::Op::OpSubgroupAvcImeStripSingleReferenceStreamoutINTEL:
    return "OpSubgroupAvcImeStripSingleReferenceStreamoutINTEL";
  case spv::Op::OpSubgroupAvcImeStripDualReferenceStreamoutINTEL:
    return "OpSubgroupAvcImeStripDualReferenceStreamoutINTEL";
  case spv::Op::
      OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeMotionVectorsINTEL:
    return "OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeMotionVectorsI"
           "NTEL";
  case spv::Op::
      OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeDistortionsINTEL:
    return "OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeDistortionsINT"
           "EL";
  case spv::Op::
      OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeReferenceIdsINTEL:
    return "OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeReferenceIdsIN"
           "TEL";
  case spv::Op::
      OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeMotionVectorsINTEL:
    return "OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeMotionVectorsINT"
           "EL";
  case spv::Op::
      OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeDistortionsINTEL:
    return "OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeDistortionsINTE"
           "L";
  case spv::Op::
      OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeReferenceIdsINTEL:
    return "OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeReferenceIdsINTE"
           "L";
  case spv::Op::OpSubgroupAvcImeGetBorderReachedINTEL:
    return "OpSubgroupAvcImeGetBorderReachedINTEL";
  case spv::Op::OpSubgroupAvcImeGetTruncatedSearchIndicationINTEL:
    return "OpSubgroupAvcImeGetTruncatedSearchIndicationINTEL";
  case spv::Op::OpSubgroupAvcImeGetUnidirectionalEarlySearchTerminationINTEL:
    return "OpSubgroupAvcImeGetUnidirectionalEarlySearchTerminationINTEL";
  case spv::Op::OpSubgroupAvcImeGetWeightingPatternMinimumMotionVectorINTEL:
    return "OpSubgroupAvcImeGetWeightingPatternMinimumMotionVectorINTEL";
  case spv::Op::OpSubgroupAvcImeGetWeightingPatternMinimumDistortionINTEL:
    return "OpSubgroupAvcImeGetWeightingPatternMinimumDistortionINTEL";
  case spv::Op::OpSubgroupAvcFmeInitializeINTEL:
    return "OpSubgroupAvcFmeInitializeINTEL";
  case spv::Op::OpSubgroupAvcBmeInitializeINTEL:
    return "OpSubgroupAvcBmeInitializeINTEL";
  case spv::Op::OpSubgroupAvcRefConvertToMcePayloadINTEL:
    return "OpSubgroupAvcRefConvertToMcePayloadINTEL";
  case spv::Op::OpSubgroupAvcRefSetBidirectionalMixDisableINTEL:
    return "OpSubgroupAvcRefSetBidirectionalMixDisableINTEL";
  case spv::Op::OpSubgroupAvcRefSetBilinearFilterEnableINTEL:
    return "OpSubgroupAvcRefSetBilinearFilterEnableINTEL";
  case spv::Op::OpSubgroupAvcRefEvaluateWithSingleReferenceINTEL:
    return "OpSubgroupAvcRefEvaluateWithSingleReferenceINTEL";
  case spv::Op::OpSubgroupAvcRefEvaluateWithDualReferenceINTEL:
    return "OpSubgroupAvcRefEvaluateWithDualReferenceINTEL";
  case spv::Op::OpSubgroupAvcRefEvaluateWithMultiReferenceINTEL:
    return "OpSubgroupAvcRefEvaluateWithMultiReferenceINTEL";
  case spv::Op::OpSubgroupAvcRefEvaluateWithMultiReferenceInterlacedINTEL:
    return "OpSubgroupAvcRefEvaluateWithMultiReferenceInterlacedINTEL";
  case spv::Op::OpSubgroupAvcRefConvertToMceResultINTEL:
    return "OpSubgroupAvcRefConvertToMceResultINTEL";
  case spv::Op::OpSubgroupAvcSicInitializeINTEL:
    return "OpSubgroupAvcSicInitializeINTEL";
  case spv::Op::OpSubgroupAvcSicConfigureSkcINTEL:
    return "OpSubgroupAvcSicConfigureSkcINTEL";
  case spv::Op::OpSubgroupAvcSicConfigureIpeLumaINTEL:
    return "OpSubgroupAvcSicConfigureIpeLumaINTEL";
  case spv::Op::OpSubgroupAvcSicConfigureIpeLumaChromaINTEL:
    return "OpSubgroupAvcSicConfigureIpeLumaChromaINTEL";
  case spv::Op::OpSubgroupAvcSicGetMotionVectorMaskINTEL:
    return "OpSubgroupAvcSicGetMotionVectorMaskINTEL";
  case spv::Op::OpSubgroupAvcSicConvertToMcePayloadINTEL:
    return "OpSubgroupAvcSicConvertToMcePayloadINTEL";
  case spv::Op::OpSubgroupAvcSicSetIntraLumaShapePenaltyINTEL:
    return "OpSubgroupAvcSicSetIntraLumaShapePenaltyINTEL";
  case spv::Op::OpSubgroupAvcSicSetIntraLumaModeCostFunctionINTEL:
    return "OpSubgroupAvcSicSetIntraLumaModeCostFunctionINTEL";
  case spv::Op::OpSubgroupAvcSicSetIntraChromaModeCostFunctionINTEL:
    return "OpSubgroupAvcSicSetIntraChromaModeCostFunctionINTEL";
  case spv::Op::OpSubgroupAvcSicSetBilinearFilterEnableINTEL:
    return "OpSubgroupAvcSicSetBilinearFilterEnableINTEL";
  case spv::Op::OpSubgroupAvcSicSetSkcForwardTransformEnableINTEL:
    return "OpSubgroupAvcSicSetSkcForwardTransformEnableINTEL";
  case spv::Op::OpSubgroupAvcSicSetBlockBasedRawSkipSadINTEL:
    return "OpSubgroupAvcSicSetBlockBasedRawSkipSadINTEL";
  case spv::Op::OpSubgroupAvcSicEvaluateIpeINTEL:
    return "OpSubgroupAvcSicEvaluateIpeINTEL";
  case spv::Op::OpSubgroupAvcSicEvaluateWithSingleReferenceINTEL:
    return "OpSubgroupAvcSicEvaluateWithSingleReferenceINTEL";
  case spv::Op::OpSubgroupAvcSicEvaluateWithDualReferenceINTEL:
    return "OpSubgroupAvcSicEvaluateWithDualReferenceINTEL";
  case spv::Op::OpSubgroupAvcSicEvaluateWithMultiReferenceINTEL:
    return "OpSubgroupAvcSicEvaluateWithMultiReferenceINTEL";
  case spv::Op::OpSubgroupAvcSicEvaluateWithMultiReferenceInterlacedINTEL:
    return "OpSubgroupAvcSicEvaluateWithMultiReferenceInterlacedINTEL";
  case spv::Op::OpSubgroupAvcSicConvertToMceResultINTEL:
    return "OpSubgroupAvcSicConvertToMceResultINTEL";
  case spv::Op::OpSubgroupAvcSicGetIpeLumaShapeINTEL:
    return "OpSubgroupAvcSicGetIpeLumaShapeINTEL";
  case spv::Op::OpSubgroupAvcSicGetBestIpeLumaDistortionINTEL:
    return "OpSubgroupAvcSicGetBestIpeLumaDistortionINTEL";
  case spv::Op::OpSubgroupAvcSicGetBestIpeChromaDistortionINTEL:
    return "OpSubgroupAvcSicGetBestIpeChromaDistortionINTEL";
  case spv::Op::OpSubgroupAvcSicGetPackedIpeLumaModesINTEL:
    return "OpSubgroupAvcSicGetPackedIpeLumaModesINTEL";
  case spv::Op::OpSubgroupAvcSicGetIpeChromaModeINTEL:
    return "OpSubgroupAvcSicGetIpeChromaModeINTEL";
  case spv::Op::OpSubgroupAvcSicGetPackedSkcLumaCountThresholdINTEL:
    return "OpSubgroupAvcSicGetPackedSkcLumaCountThresholdINTEL";
  case spv::Op::OpSubgroupAvcSicGetPackedSkcLumaSumThresholdINTEL:
    return "OpSubgroupAvcSicGetPackedSkcLumaSumThresholdINTEL";
  case spv::Op::OpSubgroupAvcSicGetInterRawSadsINTEL:
    return "OpSubgroupAvcSicGetInterRawSadsINTEL";
  case spv::Op::OpMax:
    return "OpMax";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(spv::Op const &str) { dump(get_cstr(str)); }
#endif // AUTOGENERATED

char const *get_cstr(Primitive_t const &str) {
  switch (str) {
  case Primitive_t::I1:
    return "I1";
  case Primitive_t::I8:
    return "I8";
  case Primitive_t::I16:
    return "I16";
  case Primitive_t::I32:
    return "I32";
  case Primitive_t::I64:
    return "I64";
  case Primitive_t::U8:
    return "U8";
  case Primitive_t::U16:
    return "U16";
  case Primitive_t::U32:
    return "U32";
  case Primitive_t::U64:
    return "U64";
  case Primitive_t::F8:
    return "F8";
  case Primitive_t::F16:
    return "F16";
  case Primitive_t::F32:
    return "F32";
  case Primitive_t::F64:
    return "F64";
  case Primitive_t::Void:
    return "Void";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(Primitive_t const &str) { dump(get_cstr({str})); }
char const *get_cstr(DeclTy const &str) {
  switch (str) {
  case DeclTy::PrimitiveTy:
    return "PrimitiveTy";
  case DeclTy::Variable:
    return "Variable";
  case DeclTy::Function:
    return "Function";
  case DeclTy::PtrTy:
    return "PtrTy";
  case DeclTy::VectorTy:
    return "VectorTy";
  case DeclTy::Constant:
    return "Constant";
  case DeclTy::ArrayTy:
    return "ArrayTy";
  case DeclTy::ImageTy:
    return "ImageTy";
  case DeclTy::SamplerTy:
    return "SamplerTy";
  case DeclTy::Sampled_ImageTy:
    return "Sampled_ImageTy";
  case DeclTy::FunTy:
    return "FunTy";
  case DeclTy::MatrixTy:
    return "MatrixTy";
  case DeclTy::StructTy:
    return "StructTy";
  case DeclTy::Unknown:
    return "Unknown";
  default:
    UNIMPLEMENTED;
  }
  UNIMPLEMENTED;
}
void dump(DeclTy const &str) { dump(get_cstr({str})); }
void dump(ImageTy const &str) {
  dump("ImageTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t sampled_type = ");
  dump(str.sampled_type);
  dump("\n");
  dump("  spv::Dim dim = ");
  dump(str.dim);
  dump("\n");
  dump("  bool depth = ");
  dump(str.depth);
  dump("\n");
  dump("  bool arrayed = ");
  dump(str.arrayed);
  dump("\n");
  dump("  bool ms = ");
  dump(str.ms);
  dump("\n");
  dump("  uint32_t sampled = ");
  dump(str.sampled);
  dump("\n");
  dump("  spv::ImageFormat format = ");
  dump(str.format);
  dump("\n");
  dump("  spv::AccessQualifier access = ");
  dump(str.access);
  dump("\n");
}
void dump(Sampled_ImageTy const &str) {
  dump("Sampled_ImageTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t sampled_image = ");
  dump(str.sampled_image);
  dump("\n");
}
void dump(SamplerTy const &str) {
  dump("SamplerTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
}
void dump(Decoration const &str) {
  dump("Decoration:\n");
  dump("  uint32_t target_id = ");
  dump(str.target_id);
  dump("\n");
  dump("  spv::Decoration type = ");
  dump(str.type);
  dump("\n");
  dump("  uint32_t param1 = ");
  dump(str.param1);
  dump("\n");
  dump("  uint32_t param2 = ");
  dump(str.param2);
  dump("\n");
  dump("  uint32_t param3 = ");
  dump(str.param3);
  dump("\n");
  dump("  uint32_t param4 = ");
  dump(str.param4);
  dump("\n");
}
void dump(Member_Decoration const &str) {
  dump("Member_Decoration:\n");
  dump("  uint32_t target_id = ");
  dump(str.target_id);
  dump("\n");
  dump("  uint32_t member_id = ");
  dump(str.member_id);
  dump("\n");
  dump("  spv::Decoration type = ");
  dump(str.type);
  dump("\n");
  dump("  uint32_t param1 = ");
  dump(str.param1);
  dump("\n");
  dump("  uint32_t param2 = ");
  dump(str.param2);
  dump("\n");
  dump("  uint32_t param3 = ");
  dump(str.param3);
  dump("\n");
  dump("  uint32_t param4 = ");
  dump(str.param4);
  dump("\n");
}
void dump(PrimitiveTy const &str) {
  dump("PrimitiveTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  Primitive_t type = ");
  dump(str.type);
  dump("\n");
}
void dump(VectorTy const &str) {
  dump("VectorTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t member_id = ");
  dump(str.member_id);
  dump("\n");
  dump("  uint32_t width = ");
  dump(str.width);
  dump("\n");
}
void dump(ArrayTy const &str) {
  dump("ArrayTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t member_id = ");
  dump(str.member_id);
  dump("\n");
  dump("  uint32_t width_id = ");
  dump(str.width_id);
  dump("\n");
}
void dump(MatrixTy const &str) {
  dump("MatrixTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t vector_id = ");
  dump(str.vector_id);
  dump("\n");
  dump("  uint32_t width = ");
  dump(str.width);
  dump("\n");
}
void dump(PtrTy const &str) {
  dump("PtrTy:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t target_id = ");
  dump(str.target_id);
  dump("\n");
  dump("  spv::StorageClass storage_class = ");
  dump(str.storage_class);
  dump("\n");
}
void dump(Variable const &str) {
  dump("Variable:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t type_id = ");
  dump(str.type_id);
  dump("\n");
  dump("  spv::StorageClass storage = ");
  dump(str.storage);
  dump("\n");
  dump("  uint32_t init_id = ");
  dump(str.init_id);
  dump("\n");
}
void dump(Function const &str) {
  dump("Function:\n");
  dump("  uint32_t id = ");
  dump(str.id);
  dump("\n");
  dump("  uint32_t result_type = ");
  dump(str.result_type);
  dump("\n");
  dump("  spv::FunctionControlMask control = ");
  dump(str.control);
  dump("\n");
  dump("  uint32_t function_type = ");
  dump(str.function_type);
  dump("\n");
}
#endif // AUTOGENERATED

struct Spirv_Builder {
  //////////////////////
  // Meta information //
  //////////////////////
  std::map<uint32_t, PrimitiveTy> primitive_types;
  std::map<uint32_t, Variable> variables;
  std::map<uint32_t, Function> functions;
  std::map<uint32_t, PtrTy> ptr_types;
  std::map<uint32_t, VectorTy> vector_types;
  std::map<uint32_t, Constant> constants;
  std::map<uint32_t, ArrayTy> array_types;
  std::map<uint32_t, ImageTy> images;
  std::map<uint32_t, SamplerTy> samplers;
  std::map<uint32_t, Sampled_ImageTy> combined_images;
  std::map<uint32_t, std::vector<Decoration>> decorations;
  std::map<uint32_t, std::vector<Member_Decoration>> member_decorations;
  std::map<uint32_t, FunTy> functypes;
  std::map<uint32_t, MatrixTy> matrix_types;
  std::map<uint32_t, StructTy> struct_types;
  uint32_t cur_function = 0;
  // function_id -> [var_id...]
  std::map<uint32_t, std::vector<uint32_t>> local_variables;
  // function_id -> [inst*...]
  std::map<uint32_t, std::vector<uint32_t const *>> instructions;
  std::map<uint32_t, std::string> names;
  std::map<std::pair<uint32_t, uint32_t>, std::string> member_names;
  std::vector<const uint32_t *> entries;
  // Declaration order pairs
  std::vector<std::pair<uint32_t, DeclTy>> decl_types;

  // Lifetime must be long enough
  uint32_t const *code;
  size_t code_size;

  void build_llvm_module() {
    llvm::LLVMContext *context = new llvm::LLVMContext();
    auto &c = *context;
    llvm::SMDiagnostic error;
    auto mbuf = llvm::MemoryBuffer::getMemBuffer(
        llvm::StringRef((char *)llvm_stdlib_bc, llvm_stdlib_bc_len), "", false);
    llvm::Module *module = llvm::parseIR(*mbuf.get(), error, c).release();
    assert(module);
    {
      llvm::Function *func = LOOKUP_DECL("sample_2d_f4");
      assert(func);
      func->dump();
    }

    llvm::install_fatal_error_handler(&llvm_fatal);

    llvm::IRBuilder<> builder(c, llvm::ConstantFolder());

    const uint32_t *pCode = this->code;
    const uint32_t idbound = pCode[3];
    auto get_spv_name = [this](uint32_t id) -> std::string {
      if (names.find(id) == names.end()) {
        names[id] = "spv__" + std::to_string(id);
      }
      ASSERT_ALWAYS(names.find(id) != names.end());
      return names[id];
    };
    std::vector<llvm::Type *> llvm_types(idbound);
    std::vector<llvm::Value *> llvm_values(idbound);
    std::map<uint32_t, llvm::Function *> llvm_functions;
    std::map<uint32_t, llvm::BasicBlock *> instbb;
    //    for (auto &item : vector_types) {
    //      dump(item.second);
    //    }
    // Map spirv types to llvm types
    for (auto &item : this->decl_types) {
      ASSERT_ALWAYS(llvm_types[item.first] == NULL &&
                    "Types must have unique ids");
      ASSERT_ALWAYS(llvm_values[item.first] == NULL &&
                    "Values must have unique ids");
      switch (item.second) {
      case DeclTy::FunTy: {
        FunTy type = functypes.find(item.first)->second;
        llvm::Type *ret_type = llvm_types[type.ret];
        ASSERT_ALWAYS(ret_type != NULL &&
                      "Function must have a return type defined");
        llvm::SmallVector<llvm::Type *, 16> args;
        for (auto &param_id : type.params) {
          llvm::Type *arg_type = llvm_types[param_id];
          ASSERT_ALWAYS(arg_type != NULL &&
                        "Function must have all argumet types defined");
          args.push_back(arg_type);
        }
        llvm_types[type.id] = llvm::FunctionType::get(ret_type, args, false);
        break;
      }
      case DeclTy::PtrTy: {
        PtrTy type = ptr_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.target_id];
        ASSERT_ALWAYS(elem_t != NULL && "Pointer target type must be defined");
        // Just map storage class to address space
        llvm_types[type.id] =
            llvm::PointerType::get(elem_t, (uint32_t)type.storage_class);
        break;
      }
      case DeclTy::ArrayTy: {
        ArrayTy type = array_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.member_id];
        ASSERT_ALWAYS(elem_t != NULL && "Element type must be defined");
        llvm::Value *width_value = llvm_values[type.width_id];
        ASSERT_ALWAYS(width_value != NULL && "Array width must be defined");
        llvm::ConstantInt *constant =
            llvm::dyn_cast<llvm::ConstantInt>(width_value);
        ASSERT_ALWAYS(constant != NULL &&
                      "Array width must be an integer constant");
        llvm_types[type.id] = llvm::ArrayType::get(
            elem_t, constant->getValue().getLimitedValue());
        break;
      }
      case DeclTy::ImageTy: {
        ImageTy type = images.find(item.first)->second;
        llvm_values[type.id] =
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), type.id);
        break;
      }
      case DeclTy::Constant: {
        Constant c = constants.find(item.first)->second;
        llvm::Type *type = llvm_types[c.type];
        ASSERT_ALWAYS(type != NULL && "Constant type must be defined");
        if (!type->isFloatTy() &&
            !(type->isIntegerTy() && type->getIntegerBitWidth() == 32)) {
          UNIMPLEMENTED;
        }
        if (type->isFloatTy())
          llvm_values[c.id] = llvm::ConstantFP::get(type, c.f32_val);
        else
          llvm_values[c.id] = llvm::ConstantInt::get(type, c.i32_val);
        break;
      }
      case DeclTy::Variable: {
        Variable c = variables.find(item.first)->second;
        switch (c.storage) {
        case spv::StorageClass::StorageClassUniform: {
          break;
        }
        default:
          UNIMPLEMENTED_(get_cstr(c.storage));
        }
        break;
      }
      case DeclTy::MatrixTy: {
        MatrixTy type = matrix_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.vector_id];
        ASSERT_ALWAYS(elem_t != NULL && "Matrix column type must be defined");
        llvm_types[type.id] = llvm::ArrayType::get(elem_t, type.width);
        break;
      }
      case DeclTy::StructTy: {
        StructTy type = struct_types.find(item.first)->second;
        std::vector<llvm::Type *> members;
        for (auto &member_id : type.member_types) {
          llvm::Type *member_type = llvm_types[member_id];
          ASSERT_ALWAYS(member_type != NULL && "Member types must be defined");
          members.push_back(member_type);
        }
        llvm_types[type.id] =
            llvm::StructType::create(c, members, get_spv_name(type.id));
        break;
      }
      case DeclTy::VectorTy: {
        VectorTy type = vector_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.member_id];
        ASSERT_ALWAYS(elem_t != NULL && "Element type must be defined");
        llvm_types[type.id] = llvm::VectorType::get(elem_t, type.width);
        break;
      }
      case DeclTy::SamplerTy: {
        SamplerTy type = samplers.find(item.first)->second;
        llvm_values[type.id] =
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), type.id);
        break;
      }
      case DeclTy::Sampled_ImageTy: {
        Sampled_ImageTy type = combined_images.find(item.first)->second;
        llvm_values[type.id] =
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), type.id);
        break;
      }
      case DeclTy::PrimitiveTy: {
        PrimitiveTy type = primitive_types.find(item.first)->second;
        switch (type.type) {
#define MAP(TY, LLVM_TY)                                                       \
  case Primitive_t::TY:                                                        \
    llvm_types[type.id] = LLVM_TY;                                             \
    break;
          MAP(I1, llvm::Type::getInt1Ty(c));
          MAP(I8, llvm::Type::getInt8Ty(c));
          MAP(I16, llvm::Type::getInt16Ty(c));
          MAP(I32, llvm::Type::getInt32Ty(c));
          MAP(I64, llvm::Type::getInt64Ty(c));
          MAP(U8, llvm::Type::getInt8Ty(c));
          MAP(U16, llvm::Type::getInt16Ty(c));
          MAP(U32, llvm::Type::getInt32Ty(c));
          MAP(U64, llvm::Type::getInt64Ty(c));
          MAP(F16, llvm::Type::getHalfTy(c));
          MAP(F32, llvm::Type::getFloatTy(c));
          MAP(F64, llvm::Type::getDoubleTy(c));
          MAP(Void, llvm::Type::getVoidTy(c));
#undef MAP
        default:
          UNIMPLEMENTED;
        }
        break;
      }
      default:
        UNIMPLEMENTED_(get_cstr(item.second));
      }
      ASSERT_ALWAYS(
          (llvm_values[item.first] != NULL || llvm_types[item.first] != NULL) &&
          "eh there must be a type or value at the end");
    }
  }
  void parse_meta(const uint32_t *pCode, size_t codeSize) {
    this->code = pCode;
    this->code_size = code_size;
    assert(pCode[0] == spv::MagicNumber);
    assert(pCode[1] <= spv::Version);

    const uint32_t generator = pCode[2];
    const uint32_t idbound = pCode[3];

    assert(pCode[4] == 0);

    const uint32_t *opStart = pCode + 5;
    const uint32_t *opEnd = pCode + codeSize;
    pCode = opStart;
#define CLASSIFY(id, TYPE) decl_types.push_back({id, TYPE});
    // First pass
    // Parse Meta data: types, decorations etc
    while (pCode < opEnd) {
      uint16_t WordCount = pCode[0] >> spv::WordCountShift;
      spv::Op opcode = spv::Op(pCode[0] & spv::OpCodeMask);
      uint32_t word1 = pCode[1];
      uint32_t word2 = WordCount > 1 ? pCode[2] : 0;
      uint32_t word3 = WordCount > 2 ? pCode[3] : 0;
      uint32_t word4 = WordCount > 3 ? pCode[4] : 0;
      uint32_t word5 = WordCount > 4 ? pCode[5] : 0;
      uint32_t word6 = WordCount > 5 ? pCode[6] : 0;
      uint32_t word7 = WordCount > 6 ? pCode[7] : 0;
      uint32_t word8 = WordCount > 7 ? pCode[8] : 0;
      uint32_t word9 = WordCount > 8 ? pCode[9] : 0;
      // Parse meta opcodes
      switch (opcode) {
      case spv::Op::OpName: {
        names[pCode[1]] = (const char *)&pCode[2];
        break;
      }
      case spv::Op::OpMemberName: {
        member_names[{word1, word2}] = (const char *)&pCode[2];
        break;
      }
      case spv::Op::OpEntryPoint: {
        // we'll do this in a final pass once we have the function resolved
        entries.push_back(pCode);
        break;
      }
      case spv::Op::OpMemberDecorate: {
        Member_Decoration dec;
        memset(&dec, 0, sizeof(dec));
        dec.target_id = word1;
        dec.member_id = word2;
        dec.type = (spv::Decoration)word3;
        if (WordCount > 4)
          dec.param1 = word4;
        if (WordCount > 5) {
          UNIMPLEMENTED;
        }
        member_decorations[dec.target_id].push_back(dec);
        break;
      }
      case spv::Op::OpDecorate: {
        Decoration dec;
        memset(&dec, 0, sizeof(dec));
        dec.target_id = word1;
        dec.type = (spv::Decoration)word2;
        if (WordCount > 3)
          dec.param1 = word3;
        if (WordCount > 4)
          dec.param2 = word4;
        if (WordCount > 5) {
          UNIMPLEMENTED;
        }
        decorations[dec.target_id].push_back(dec);
        break;
      }
      case spv::Op::OpTypeVoid: {
        primitive_types[word1] =
            PrimitiveTy{.id = word1, .type = Primitive_t::Void};
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeFloat: {
        if (word2 == 16)
          primitive_types[word1] =
              PrimitiveTy{.id = word1, .type = Primitive_t::F16};
        else if (word2 == 32)
          primitive_types[word1] =
              PrimitiveTy{.id = word1, .type = Primitive_t::F32};
        else if (word2 == 64)
          primitive_types[word1] =
              PrimitiveTy{.id = word1, .type = Primitive_t::F64};
        else {
          UNIMPLEMENTED;
        }
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeBool: {
        primitive_types[word1] =
            PrimitiveTy{.id = word1, .type = Primitive_t::I1};
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeInt: {
        bool sign = word3 != 0;
        if (word2 == 8)
          primitive_types[word1] = PrimitiveTy{
              .id = word1, .type = (sign ? Primitive_t::I8 : Primitive_t::U8)};
        else if (word2 == 16)
          primitive_types[word1] =
              PrimitiveTy{.id = word1,
                          .type = (sign ? Primitive_t::I16 : Primitive_t::U16)};
        else if (word2 == 32)
          primitive_types[word1] =
              PrimitiveTy{.id = word1,
                          .type = (sign ? Primitive_t::I32 : Primitive_t::U32)};
        else if (word2 == 64)
          primitive_types[word1] =
              PrimitiveTy{.id = word1,
                          .type = (sign ? Primitive_t::I64 : Primitive_t::U64)};
        else {
          UNIMPLEMENTED;
        }
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeVector: {
        VectorTy type;
        type.id = word1;
        type.member_id = word2;
        type.width = word3;
        vector_types[word1] = type;
        CLASSIFY(type.id, DeclTy::VectorTy);
        break;
      }
      case spv::Op::OpTypeArray: {
        ArrayTy type;
        type.id = word1;
        type.member_id = word2;
        type.width_id = word3;
        array_types[word1] = type;
        CLASSIFY(type.id, DeclTy::ArrayTy);
        break;
      }
      case spv::Op::OpTypeMatrix: {
        MatrixTy type;
        type.id = word1;
        type.vector_id = word2;
        type.width = word3;
        matrix_types[word1] = type;
        CLASSIFY(type.id, DeclTy::MatrixTy);
        break;
      }
      case spv::Op::OpTypePointer: {
        PtrTy type;
        type.id = word1;
        type.storage_class = (spv::StorageClass)word2;
        type.target_id = word3;
        ptr_types[word1] = type;
        CLASSIFY(type.id, DeclTy::PtrTy);
        break;
      }
      case spv::Op::OpTypeStruct: {
        StructTy type;
        type.id = word1;
        for (uint16_t i = 2; i < WordCount; i++)
          type.member_types.push_back(pCode[i]);
        struct_types[word1] = type;
        CLASSIFY(type.id, DeclTy::StructTy);
        break;
      }
      case spv::Op::OpTypeFunction: {
        FunTy &f = functypes[word1];
        f.id = word1;
        for (uint16_t i = 3; i < WordCount; i++)
          f.params.push_back(pCode[i]);
        f.ret = word2;
        CLASSIFY(f.id, DeclTy::FunTy);
        break;
      }
      case spv::Op::OpTypeImage: {
        ImageTy type;
        type.id = word1;
        type.sampled_type = word2;
        type.dim = (spv::Dim)word3;
        type.depth = word4 == 1;
        type.arrayed = word5 != 0;
        type.ms = word6 != 0;
        type.sampled = word7;
        type.format = (spv::ImageFormat)(word8);
        type.access = WordCount > 8 ? (spv::AccessQualifier)(word9)
                                    : spv::AccessQualifier::AccessQualifierMax;
        images[word1] = type;
        CLASSIFY(type.id, DeclTy::ImageTy);
        break;
      }
      case spv::Op::OpTypeSampledImage: {
        Sampled_ImageTy type;
        type.id = word1;
        type.sampled_image = word2;
        combined_images[word1] = type;
        CLASSIFY(type.id, DeclTy::Sampled_ImageTy);
        break;
      }
      case spv::Op::OpTypeSampler: {
        samplers[word1] = SamplerTy{word1};
        CLASSIFY(word1, DeclTy::SamplerTy);
        break;
      }
      case spv::Op::OpVariable: {
        Variable var;
        var.id = word2;
        var.type_id = word1;
        var.storage = (spv::StorageClass)word3;
        var.init_id = word4;
        variables[word2] = var;
        if (var.storage == spv::StorageClass::StorageClassFunction) {
          // Push local variables
          ASSERT_ALWAYS(cur_function != 0);
          local_variables[cur_function].push_back(word2);
        }
        CLASSIFY(var.id, DeclTy::Variable);
        break;
      }
      case spv::Op::OpFunction: {
        ASSERT_ALWAYS(cur_function == 0);
        Function fun;
        fun.id = word2;
        fun.result_type = word1;
        fun.function_type = word4;
        fun.control = (spv::FunctionControlMask)word2;
        cur_function = word2;
        functions[word2] = fun;
        CLASSIFY(fun.id, DeclTy::Function);
        break;
      }
      case spv::Op::OpFunctionEnd: {
        ASSERT_ALWAYS(cur_function != 0);
        cur_function = 0;
        break;
      }
      case spv::Op::OpConstant: {
        Constant c;
        c.id = word2;
        c.type = word1;
        memcpy(&c.i32_val, &word3, 4);
        constants[word2] = c;
        CLASSIFY(c.id, DeclTy::Constant);
        break;
      }
      // TODO: Do we care about those?
      case spv::Op::OpCapability:
      case spv::Op::OpExtension:
      case spv::Op::OpExtInstImport:
      case spv::Op::OpMemoryModel:
      case spv::Op::OpExecutionMode:
      case spv::Op::OpSource:
      case spv::Op::OpSourceExtension:
        // SKIP
        break;
      // Trap those
      case spv::Op::OpConstantComposite:
      case spv::Op::OpUndef:
      case spv::Op::OpSourceContinued:
      case spv::Op::OpString:
      case spv::Op::OpLine:
      case spv::Op::OpTypeRuntimeArray:
      case spv::Op::OpTypeOpaque:
      case spv::Op::OpTypeEvent:
      case spv::Op::OpTypeDeviceEvent:
      case spv::Op::OpTypeReserveId:
      case spv::Op::OpTypeQueue:
      case spv::Op::OpTypePipe:
      case spv::Op::OpTypeForwardPointer:
      case spv::Op::OpConstantTrue:
      case spv::Op::OpConstantFalse:
      case spv::Op::OpConstantSampler:
      case spv::Op::OpConstantNull:
      case spv::Op::OpSpecConstantTrue:
      case spv::Op::OpSpecConstantFalse:
      case spv::Op::OpSpecConstant:
      case spv::Op::OpSpecConstantComposite:
      case spv::Op::OpSpecConstantOp:
      case spv::Op::OpFunctionParameter:
      case spv::Op::OpImageTexelPointer:
      case spv::Op::OpDecorationGroup:
      case spv::Op::OpGroupDecorate:
      case spv::Op::OpGroupMemberDecorate:
        UNIMPLEMENTED_(get_cstr(opcode));
      // Instructions
      case spv::Op::OpLoad:
      case spv::Op::OpStore:
      case spv::Op::OpCopyMemory:
      case spv::Op::OpCopyMemorySized:
      case spv::Op::OpAccessChain:
      case spv::Op::OpInBoundsAccessChain:
      case spv::Op::OpPtrAccessChain:
      case spv::Op::OpArrayLength:
      case spv::Op::OpGenericPtrMemSemantics:
      case spv::Op::OpInBoundsPtrAccessChain:
      case spv::Op::OpNop:
      case spv::Op::OpSampledImage:
      case spv::Op::OpImageSampleImplicitLod:
      case spv::Op::OpImageSampleExplicitLod:
      case spv::Op::OpImageSampleDrefImplicitLod:
      case spv::Op::OpImageSampleDrefExplicitLod:
      case spv::Op::OpImageSampleProjImplicitLod:
      case spv::Op::OpImageSampleProjExplicitLod:
      case spv::Op::OpImageSampleProjDrefImplicitLod:
      case spv::Op::OpImageSampleProjDrefExplicitLod:
      case spv::Op::OpImageFetch:
      case spv::Op::OpImageGather:
      case spv::Op::OpImageDrefGather:
      case spv::Op::OpImageRead:
      case spv::Op::OpImageWrite:
      case spv::Op::OpImage:
      case spv::Op::OpImageQueryFormat:
      case spv::Op::OpImageQueryOrder:
      case spv::Op::OpImageQuerySizeLod:
      case spv::Op::OpImageQuerySize:
      case spv::Op::OpImageQueryLod:
      case spv::Op::OpImageQueryLevels:
      case spv::Op::OpImageQuerySamples:
      case spv::Op::OpConvertFToU:
      case spv::Op::OpConvertFToS:
      case spv::Op::OpConvertSToF:
      case spv::Op::OpConvertUToF:
      case spv::Op::OpUConvert:
      case spv::Op::OpSConvert:
      case spv::Op::OpFConvert:
      case spv::Op::OpQuantizeToF16:
      case spv::Op::OpConvertPtrToU:
      case spv::Op::OpSatConvertSToU:
      case spv::Op::OpSatConvertUToS:
      case spv::Op::OpConvertUToPtr:
      case spv::Op::OpPtrCastToGeneric:
      case spv::Op::OpGenericCastToPtr:
      case spv::Op::OpGenericCastToPtrExplicit:
      case spv::Op::OpBitcast:
      case spv::Op::OpSNegate:
      case spv::Op::OpFNegate:
      case spv::Op::OpIAdd:
      case spv::Op::OpFAdd:
      case spv::Op::OpISub:
      case spv::Op::OpFSub:
      case spv::Op::OpIMul:
      case spv::Op::OpFMul:
      case spv::Op::OpUDiv:
      case spv::Op::OpSDiv:
      case spv::Op::OpFDiv:
      case spv::Op::OpUMod:
      case spv::Op::OpSRem:
      case spv::Op::OpSMod:
      case spv::Op::OpFRem:
      case spv::Op::OpFMod:
      case spv::Op::OpVectorTimesScalar:
      case spv::Op::OpMatrixTimesScalar:
      case spv::Op::OpVectorTimesMatrix:
      case spv::Op::OpMatrixTimesVector:
      case spv::Op::OpMatrixTimesMatrix:
      case spv::Op::OpOuterProduct:
      case spv::Op::OpDot:
      case spv::Op::OpIAddCarry:
      case spv::Op::OpISubBorrow:
      case spv::Op::OpUMulExtended:
      case spv::Op::OpSMulExtended:
      case spv::Op::OpAny:
      case spv::Op::OpAll:
      case spv::Op::OpIsNan:
      case spv::Op::OpIsInf:
      case spv::Op::OpIsFinite:
      case spv::Op::OpIsNormal:
      case spv::Op::OpSignBitSet:
      case spv::Op::OpLessOrGreater:
      case spv::Op::OpOrdered:
      case spv::Op::OpUnordered:
      case spv::Op::OpLogicalEqual:
      case spv::Op::OpLogicalNotEqual:
      case spv::Op::OpLogicalOr:
      case spv::Op::OpLogicalAnd:
      case spv::Op::OpLogicalNot:
      case spv::Op::OpSelect:
      case spv::Op::OpIEqual:
      case spv::Op::OpINotEqual:
      case spv::Op::OpUGreaterThan:
      case spv::Op::OpSGreaterThan:
      case spv::Op::OpUGreaterThanEqual:
      case spv::Op::OpSGreaterThanEqual:
      case spv::Op::OpULessThan:
      case spv::Op::OpSLessThan:
      case spv::Op::OpULessThanEqual:
      case spv::Op::OpSLessThanEqual:
      case spv::Op::OpFOrdEqual:
      case spv::Op::OpFUnordEqual:
      case spv::Op::OpFOrdNotEqual:
      case spv::Op::OpFUnordNotEqual:
      case spv::Op::OpFOrdLessThan:
      case spv::Op::OpFUnordLessThan:
      case spv::Op::OpFOrdGreaterThan:
      case spv::Op::OpFUnordGreaterThan:
      case spv::Op::OpFOrdLessThanEqual:
      case spv::Op::OpFUnordLessThanEqual:
      case spv::Op::OpFOrdGreaterThanEqual:
      case spv::Op::OpFUnordGreaterThanEqual:
      case spv::Op::OpShiftRightLogical:
      case spv::Op::OpShiftRightArithmetic:
      case spv::Op::OpShiftLeftLogical:
      case spv::Op::OpBitwiseOr:
      case spv::Op::OpBitwiseXor:
      case spv::Op::OpBitwiseAnd:
      case spv::Op::OpNot:
      case spv::Op::OpBitFieldInsert:
      case spv::Op::OpBitFieldSExtract:
      case spv::Op::OpBitFieldUExtract:
      case spv::Op::OpBitReverse:
      case spv::Op::OpBitCount:
      case spv::Op::OpDPdx:
      case spv::Op::OpDPdy:
      case spv::Op::OpFwidth:
      case spv::Op::OpDPdxFine:
      case spv::Op::OpDPdyFine:
      case spv::Op::OpFwidthFine:
      case spv::Op::OpDPdxCoarse:
      case spv::Op::OpDPdyCoarse:
      case spv::Op::OpFwidthCoarse:
      case spv::Op::OpEmitVertex:
      case spv::Op::OpEndPrimitive:
      case spv::Op::OpEmitStreamVertex:
      case spv::Op::OpEndStreamPrimitive:
      case spv::Op::OpControlBarrier:
      case spv::Op::OpMemoryBarrier:
      case spv::Op::OpAtomicLoad:
      case spv::Op::OpAtomicStore:
      case spv::Op::OpAtomicExchange:
      case spv::Op::OpAtomicCompareExchange:
      case spv::Op::OpAtomicCompareExchangeWeak:
      case spv::Op::OpAtomicIIncrement:
      case spv::Op::OpAtomicIDecrement:
      case spv::Op::OpAtomicIAdd:
      case spv::Op::OpAtomicISub:
      case spv::Op::OpAtomicSMin:
      case spv::Op::OpAtomicUMin:
      case spv::Op::OpAtomicSMax:
      case spv::Op::OpAtomicUMax:
      case spv::Op::OpAtomicAnd:
      case spv::Op::OpAtomicOr:
      case spv::Op::OpAtomicXor:
      case spv::Op::OpPhi:
      case spv::Op::OpLoopMerge:
      case spv::Op::OpSelectionMerge:
      case spv::Op::OpLabel:
      case spv::Op::OpBranch:
      case spv::Op::OpBranchConditional:
      case spv::Op::OpSwitch:
      case spv::Op::OpKill:
      case spv::Op::OpReturn:
      case spv::Op::OpReturnValue:
      case spv::Op::OpUnreachable:
      case spv::Op::OpLifetimeStart:
      case spv::Op::OpLifetimeStop:
      case spv::Op::OpGroupAsyncCopy:
      case spv::Op::OpGroupWaitEvents:
      case spv::Op::OpGroupAll:
      case spv::Op::OpGroupAny:
      case spv::Op::OpGroupBroadcast:
      case spv::Op::OpGroupIAdd:
      case spv::Op::OpGroupFAdd:
      case spv::Op::OpGroupFMin:
      case spv::Op::OpGroupUMin:
      case spv::Op::OpGroupSMin:
      case spv::Op::OpGroupFMax:
      case spv::Op::OpGroupUMax:
      case spv::Op::OpGroupSMax:
      case spv::Op::OpReadPipe:
      case spv::Op::OpWritePipe:
      case spv::Op::OpReservedReadPipe:
      case spv::Op::OpReservedWritePipe:
      case spv::Op::OpReserveReadPipePackets:
      case spv::Op::OpReserveWritePipePackets:
      case spv::Op::OpCommitReadPipe:
      case spv::Op::OpCommitWritePipe:
      case spv::Op::OpIsValidReserveId:
      case spv::Op::OpGetNumPipePackets:
      case spv::Op::OpGetMaxPipePackets:
      case spv::Op::OpGroupReserveReadPipePackets:
      case spv::Op::OpGroupReserveWritePipePackets:
      case spv::Op::OpGroupCommitReadPipe:
      case spv::Op::OpGroupCommitWritePipe:
      case spv::Op::OpEnqueueMarker:
      case spv::Op::OpEnqueueKernel:
      case spv::Op::OpGetKernelNDrangeSubGroupCount:
      case spv::Op::OpGetKernelNDrangeMaxSubGroupSize:
      case spv::Op::OpGetKernelWorkGroupSize:
      case spv::Op::OpGetKernelPreferredWorkGroupSizeMultiple:
      case spv::Op::OpRetainEvent:
      case spv::Op::OpReleaseEvent:
      case spv::Op::OpCreateUserEvent:
      case spv::Op::OpIsValidEvent:
      case spv::Op::OpSetUserEventStatus:
      case spv::Op::OpCaptureEventProfilingInfo:
      case spv::Op::OpGetDefaultQueue:
      case spv::Op::OpBuildNDRange:
      case spv::Op::OpImageSparseSampleImplicitLod:
      case spv::Op::OpImageSparseSampleExplicitLod:
      case spv::Op::OpImageSparseSampleDrefImplicitLod:
      case spv::Op::OpImageSparseSampleDrefExplicitLod:
      case spv::Op::OpImageSparseSampleProjImplicitLod:
      case spv::Op::OpImageSparseSampleProjExplicitLod:
      case spv::Op::OpImageSparseSampleProjDrefImplicitLod:
      case spv::Op::OpImageSparseSampleProjDrefExplicitLod:
      case spv::Op::OpImageSparseFetch:
      case spv::Op::OpImageSparseGather:
      case spv::Op::OpImageSparseDrefGather:
      case spv::Op::OpImageSparseTexelsResident:
      case spv::Op::OpNoLine:
      case spv::Op::OpAtomicFlagTestAndSet:
      case spv::Op::OpAtomicFlagClear:
      case spv::Op::OpImageSparseRead:
      case spv::Op::OpSizeOf:
      case spv::Op::OpTypePipeStorage:
      case spv::Op::OpConstantPipeStorage:
      case spv::Op::OpCreatePipeFromPipeStorage:
      case spv::Op::OpGetKernelLocalSizeForSubgroupCount:
      case spv::Op::OpGetKernelMaxNumSubgroups:
      case spv::Op::OpTypeNamedBarrier:
      case spv::Op::OpNamedBarrierInitialize:
      case spv::Op::OpMemoryNamedBarrier:
      case spv::Op::OpModuleProcessed:
      case spv::Op::OpExecutionModeId:
      case spv::Op::OpDecorateId:
      case spv::Op::OpGroupNonUniformElect:
      case spv::Op::OpGroupNonUniformAll:
      case spv::Op::OpGroupNonUniformAny:
      case spv::Op::OpGroupNonUniformAllEqual:
      case spv::Op::OpGroupNonUniformBroadcast:
      case spv::Op::OpGroupNonUniformBroadcastFirst:
      case spv::Op::OpGroupNonUniformBallot:
      case spv::Op::OpGroupNonUniformInverseBallot:
      case spv::Op::OpGroupNonUniformBallotBitExtract:
      case spv::Op::OpGroupNonUniformBallotBitCount:
      case spv::Op::OpGroupNonUniformBallotFindLSB:
      case spv::Op::OpGroupNonUniformBallotFindMSB:
      case spv::Op::OpGroupNonUniformShuffle:
      case spv::Op::OpGroupNonUniformShuffleXor:
      case spv::Op::OpGroupNonUniformShuffleUp:
      case spv::Op::OpGroupNonUniformShuffleDown:
      case spv::Op::OpGroupNonUniformIAdd:
      case spv::Op::OpGroupNonUniformFAdd:
      case spv::Op::OpGroupNonUniformIMul:
      case spv::Op::OpGroupNonUniformFMul:
      case spv::Op::OpGroupNonUniformSMin:
      case spv::Op::OpGroupNonUniformUMin:
      case spv::Op::OpGroupNonUniformFMin:
      case spv::Op::OpGroupNonUniformSMax:
      case spv::Op::OpGroupNonUniformUMax:
      case spv::Op::OpGroupNonUniformFMax:
      case spv::Op::OpGroupNonUniformBitwiseAnd:
      case spv::Op::OpGroupNonUniformBitwiseOr:
      case spv::Op::OpGroupNonUniformBitwiseXor:
      case spv::Op::OpGroupNonUniformLogicalAnd:
      case spv::Op::OpGroupNonUniformLogicalOr:
      case spv::Op::OpGroupNonUniformLogicalXor:
      case spv::Op::OpGroupNonUniformQuadBroadcast:
      case spv::Op::OpGroupNonUniformQuadSwap:
      case spv::Op::OpCopyLogical:
      case spv::Op::OpPtrEqual:
      case spv::Op::OpPtrNotEqual:
      case spv::Op::OpPtrDiff:
      case spv::Op::OpSubgroupBallotKHR:
      case spv::Op::OpSubgroupFirstInvocationKHR:
      case spv::Op::OpSubgroupAllKHR:
      case spv::Op::OpSubgroupAnyKHR:
      case spv::Op::OpSubgroupAllEqualKHR:
      case spv::Op::OpSubgroupReadInvocationKHR:
      case spv::Op::OpGroupIAddNonUniformAMD:
      case spv::Op::OpGroupFAddNonUniformAMD:
      case spv::Op::OpGroupFMinNonUniformAMD:
      case spv::Op::OpGroupUMinNonUniformAMD:
      case spv::Op::OpGroupSMinNonUniformAMD:
      case spv::Op::OpGroupFMaxNonUniformAMD:
      case spv::Op::OpGroupUMaxNonUniformAMD:
      case spv::Op::OpGroupSMaxNonUniformAMD:
      case spv::Op::OpFragmentMaskFetchAMD:
      case spv::Op::OpFragmentFetchAMD:
      case spv::Op::OpReadClockKHR:
      case spv::Op::OpImageSampleFootprintNV:
      case spv::Op::OpGroupNonUniformPartitionNV:
      case spv::Op::OpWritePackedPrimitiveIndices4x8NV:
      case spv::Op::OpReportIntersectionNV:
      case spv::Op::OpIgnoreIntersectionNV:
      case spv::Op::OpTerminateRayNV:
      case spv::Op::OpTraceNV:
      case spv::Op::OpTypeAccelerationStructureNV:
      case spv::Op::OpExecuteCallableNV:
      case spv::Op::OpTypeCooperativeMatrixNV:
      case spv::Op::OpCooperativeMatrixLoadNV:
      case spv::Op::OpCooperativeMatrixStoreNV:
      case spv::Op::OpCooperativeMatrixMulAddNV:
      case spv::Op::OpCooperativeMatrixLengthNV:
      case spv::Op::OpBeginInvocationInterlockEXT:
      case spv::Op::OpEndInvocationInterlockEXT:
      case spv::Op::OpDemoteToHelperInvocationEXT:
      case spv::Op::OpIsHelperInvocationEXT:
      case spv::Op::OpSubgroupShuffleINTEL:
      case spv::Op::OpSubgroupShuffleDownINTEL:
      case spv::Op::OpSubgroupShuffleUpINTEL:
      case spv::Op::OpSubgroupShuffleXorINTEL:
      case spv::Op::OpSubgroupBlockReadINTEL:
      case spv::Op::OpSubgroupBlockWriteINTEL:
      case spv::Op::OpSubgroupImageBlockReadINTEL:
      case spv::Op::OpSubgroupImageBlockWriteINTEL:
      case spv::Op::OpSubgroupImageMediaBlockReadINTEL:
      case spv::Op::OpSubgroupImageMediaBlockWriteINTEL:
      case spv::Op::OpUCountLeadingZerosINTEL:
      case spv::Op::OpUCountTrailingZerosINTEL:
      case spv::Op::OpAbsISubINTEL:
      case spv::Op::OpAbsUSubINTEL:
      case spv::Op::OpIAddSatINTEL:
      case spv::Op::OpUAddSatINTEL:
      case spv::Op::OpIAverageINTEL:
      case spv::Op::OpUAverageINTEL:
      case spv::Op::OpIAverageRoundedINTEL:
      case spv::Op::OpUAverageRoundedINTEL:
      case spv::Op::OpISubSatINTEL:
      case spv::Op::OpUSubSatINTEL:
      case spv::Op::OpIMul32x16INTEL:
      case spv::Op::OpUMul32x16INTEL:
      case spv::Op::OpDecorateString:
      case spv::Op::OpMemberDecorateString:
      case spv::Op::OpVmeImageINTEL:
      case spv::Op::OpTypeVmeImageINTEL:
      case spv::Op::OpTypeAvcImePayloadINTEL:
      case spv::Op::OpTypeAvcRefPayloadINTEL:
      case spv::Op::OpTypeAvcSicPayloadINTEL:
      case spv::Op::OpTypeAvcMcePayloadINTEL:
      case spv::Op::OpTypeAvcMceResultINTEL:
      case spv::Op::OpTypeAvcImeResultINTEL:
      case spv::Op::OpTypeAvcImeResultSingleReferenceStreamoutINTEL:
      case spv::Op::OpTypeAvcImeResultDualReferenceStreamoutINTEL:
      case spv::Op::OpTypeAvcImeSingleReferenceStreaminINTEL:
      case spv::Op::OpTypeAvcImeDualReferenceStreaminINTEL:
      case spv::Op::OpTypeAvcRefResultINTEL:
      case spv::Op::OpTypeAvcSicResultINTEL:
      case spv::Op::
          OpSubgroupAvcMceGetDefaultInterBaseMultiReferencePenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceSetInterBaseMultiReferencePenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultInterShapePenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceSetInterShapePenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultInterDirectionPenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceSetInterDirectionPenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultIntraLumaShapePenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultInterMotionVectorCostTableINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultHighPenaltyCostTableINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultMediumPenaltyCostTableINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultLowPenaltyCostTableINTEL:
      case spv::Op::OpSubgroupAvcMceSetMotionVectorCostFunctionINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultIntraLumaModePenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultNonDcLumaIntraPenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceGetDefaultIntraChromaModeBasePenaltyINTEL:
      case spv::Op::OpSubgroupAvcMceSetAcOnlyHaarINTEL:
      case spv::Op::OpSubgroupAvcMceSetSourceInterlacedFieldPolarityINTEL:
      case spv::Op::
          OpSubgroupAvcMceSetSingleReferenceInterlacedFieldPolarityINTEL:
      case spv::Op::
          OpSubgroupAvcMceSetDualReferenceInterlacedFieldPolaritiesINTEL:
      case spv::Op::OpSubgroupAvcMceConvertToImePayloadINTEL:
      case spv::Op::OpSubgroupAvcMceConvertToImeResultINTEL:
      case spv::Op::OpSubgroupAvcMceConvertToRefPayloadINTEL:
      case spv::Op::OpSubgroupAvcMceConvertToRefResultINTEL:
      case spv::Op::OpSubgroupAvcMceConvertToSicPayloadINTEL:
      case spv::Op::OpSubgroupAvcMceConvertToSicResultINTEL:
      case spv::Op::OpSubgroupAvcMceGetMotionVectorsINTEL:
      case spv::Op::OpSubgroupAvcMceGetInterDistortionsINTEL:
      case spv::Op::OpSubgroupAvcMceGetBestInterDistortionsINTEL:
      case spv::Op::OpSubgroupAvcMceGetInterMajorShapeINTEL:
      case spv::Op::OpSubgroupAvcMceGetInterMinorShapeINTEL:
      case spv::Op::OpSubgroupAvcMceGetInterDirectionsINTEL:
      case spv::Op::OpSubgroupAvcMceGetInterMotionVectorCountINTEL:
      case spv::Op::OpSubgroupAvcMceGetInterReferenceIdsINTEL:
      case spv::Op::
          OpSubgroupAvcMceGetInterReferenceInterlacedFieldPolaritiesINTEL:
      case spv::Op::OpSubgroupAvcImeInitializeINTEL:
      case spv::Op::OpSubgroupAvcImeSetSingleReferenceINTEL:
      case spv::Op::OpSubgroupAvcImeSetDualReferenceINTEL:
      case spv::Op::OpSubgroupAvcImeRefWindowSizeINTEL:
      case spv::Op::OpSubgroupAvcImeAdjustRefOffsetINTEL:
      case spv::Op::OpSubgroupAvcImeConvertToMcePayloadINTEL:
      case spv::Op::OpSubgroupAvcImeSetMaxMotionVectorCountINTEL:
      case spv::Op::OpSubgroupAvcImeSetUnidirectionalMixDisableINTEL:
      case spv::Op::OpSubgroupAvcImeSetEarlySearchTerminationThresholdINTEL:
      case spv::Op::OpSubgroupAvcImeSetWeightedSadINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceStreaminINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceStreamoutINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceStreamoutINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminoutINTEL:
      case spv::Op::OpSubgroupAvcImeEvaluateWithDualReferenceStreaminoutINTEL:
      case spv::Op::OpSubgroupAvcImeConvertToMceResultINTEL:
      case spv::Op::OpSubgroupAvcImeGetSingleReferenceStreaminINTEL:
      case spv::Op::OpSubgroupAvcImeGetDualReferenceStreaminINTEL:
      case spv::Op::OpSubgroupAvcImeStripSingleReferenceStreamoutINTEL:
      case spv::Op::OpSubgroupAvcImeStripDualReferenceStreamoutINTEL:
      case spv::Op::
          OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeMotionVectorsINTEL:
      case spv::Op::
          OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeDistortionsINTEL:
      case spv::Op::
          OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeReferenceIdsINTEL:
      case spv::Op::
          OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeMotionVectorsINTEL:
      case spv::Op::
          OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeDistortionsINTEL:
      case spv::Op::
          OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeReferenceIdsINTEL:
      case spv::Op::OpSubgroupAvcImeGetBorderReachedINTEL:
      case spv::Op::OpSubgroupAvcImeGetTruncatedSearchIndicationINTEL:
      case spv::Op::
          OpSubgroupAvcImeGetUnidirectionalEarlySearchTerminationINTEL:
      case spv::Op::OpSubgroupAvcImeGetWeightingPatternMinimumMotionVectorINTEL:
      case spv::Op::OpSubgroupAvcImeGetWeightingPatternMinimumDistortionINTEL:
      case spv::Op::OpSubgroupAvcFmeInitializeINTEL:
      case spv::Op::OpSubgroupAvcBmeInitializeINTEL:
      case spv::Op::OpSubgroupAvcRefConvertToMcePayloadINTEL:
      case spv::Op::OpSubgroupAvcRefSetBidirectionalMixDisableINTEL:
      case spv::Op::OpSubgroupAvcRefSetBilinearFilterEnableINTEL:
      case spv::Op::OpSubgroupAvcRefEvaluateWithSingleReferenceINTEL:
      case spv::Op::OpSubgroupAvcRefEvaluateWithDualReferenceINTEL:
      case spv::Op::OpSubgroupAvcRefEvaluateWithMultiReferenceINTEL:
      case spv::Op::OpSubgroupAvcRefEvaluateWithMultiReferenceInterlacedINTEL:
      case spv::Op::OpSubgroupAvcRefConvertToMceResultINTEL:
      case spv::Op::OpSubgroupAvcSicInitializeINTEL:
      case spv::Op::OpSubgroupAvcSicConfigureSkcINTEL:
      case spv::Op::OpSubgroupAvcSicConfigureIpeLumaINTEL:
      case spv::Op::OpSubgroupAvcSicConfigureIpeLumaChromaINTEL:
      case spv::Op::OpSubgroupAvcSicGetMotionVectorMaskINTEL:
      case spv::Op::OpSubgroupAvcSicConvertToMcePayloadINTEL:
      case spv::Op::OpSubgroupAvcSicSetIntraLumaShapePenaltyINTEL:
      case spv::Op::OpSubgroupAvcSicSetIntraLumaModeCostFunctionINTEL:
      case spv::Op::OpSubgroupAvcSicSetIntraChromaModeCostFunctionINTEL:
      case spv::Op::OpSubgroupAvcSicSetBilinearFilterEnableINTEL:
      case spv::Op::OpSubgroupAvcSicSetSkcForwardTransformEnableINTEL:
      case spv::Op::OpSubgroupAvcSicSetBlockBasedRawSkipSadINTEL:
      case spv::Op::OpSubgroupAvcSicEvaluateIpeINTEL:
      case spv::Op::OpSubgroupAvcSicEvaluateWithSingleReferenceINTEL:
      case spv::Op::OpSubgroupAvcSicEvaluateWithDualReferenceINTEL:
      case spv::Op::OpSubgroupAvcSicEvaluateWithMultiReferenceINTEL:
      case spv::Op::OpSubgroupAvcSicEvaluateWithMultiReferenceInterlacedINTEL:
      case spv::Op::OpSubgroupAvcSicConvertToMceResultINTEL:
      case spv::Op::OpSubgroupAvcSicGetIpeLumaShapeINTEL:
      case spv::Op::OpSubgroupAvcSicGetBestIpeLumaDistortionINTEL:
      case spv::Op::OpSubgroupAvcSicGetBestIpeChromaDistortionINTEL:
      case spv::Op::OpSubgroupAvcSicGetPackedIpeLumaModesINTEL:
      case spv::Op::OpSubgroupAvcSicGetIpeChromaModeINTEL:
      case spv::Op::OpSubgroupAvcSicGetPackedSkcLumaCountThresholdINTEL:
      case spv::Op::OpSubgroupAvcSicGetPackedSkcLumaSumThresholdINTEL:
      case spv::Op::OpSubgroupAvcSicGetInterRawSadsINTEL:
      case spv::Op::OpMax:
      case spv::Op::OpVectorExtractDynamic:
      case spv::Op::OpVectorInsertDynamic:
      case spv::Op::OpVectorShuffle:
      case spv::Op::OpCompositeConstruct:
      case spv::Op::OpCompositeExtract:
      case spv::Op::OpCompositeInsert:
      case spv::Op::OpCopyObject:
      case spv::Op::OpTranspose:
      case spv::Op::OpFunctionCall:
      case spv::Op::OpExtInst: {
        ASSERT_ALWAYS(cur_function != 0);
        instructions[cur_function].push_back(pCode);
        break;
      }
      default:
        UNIMPLEMENTED_(get_cstr(opcode));
      }
      pCode += WordCount;
    }
#undef CLASSIFY
  }
};
int main(int argc, char **argv) {
  ASSERT_ALWAYS(argc == 2);
  auto bytes = readFile(argv[1]);

  const uint32_t *pCode = (uint32_t *)&bytes[0];
  size_t codeSize = bytes.size() / 4;
  Spirv_Builder builder;
  builder.parse_meta(pCode, codeSize);
  builder.build_llvm_module();
}
#endif // S2L_EXE
