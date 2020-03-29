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
#define UNIMPLEMENTED                                                          \
  {                                                                            \
    fprintf(stderr, "UNIMPLEMENTED: %s:%i\n", __FILE__, __LINE__);             \
    exit(1);                                                                   \
  }

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
int main(int argc, char **argv) {
  ASSERT_ALWAYS(argc == 2);
  auto bytes = readFile(argv[1]);

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

  using namespace llvm;
  const uint32_t *pCode = (uint32_t *)&bytes[0];
  size_t codeSize = bytes.size() / 4;
  assert(pCode[0] == spv::MagicNumber);
  assert(pCode[1] <= spv::Version);

  const uint32_t generator = pCode[2];
  const uint32_t idbound = pCode[3];

  assert(pCode[4] == 0);
  IRBuilder<> builder(c, ConstantFolder());

  const uint32_t *opStart = pCode + 5;
  const uint32_t *opEnd = pCode + codeSize;
  pCode = opStart;

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
  // function_id -> [var_id..]
  std::map<uint32_t, std::vector<uint32_t>> local_variables;
  // enum class BaseTy {
  //   I8, I16, I32, I64,
  //   U8, U16, U32, U64,
  //   F8, F16, F32, F64,
  //   Atomic_Counter,
  //   Struct,
  //   Image,
  //   SampledImage,
  //   Sampler,
  //   Void,
  //   Unknown
  // };
  // struct AggregateTy {
  //   uint32_t id;
  //   BaseTy type;
  //   uint32_t width, height, depth;
  //   uint32_t pointer_depth;
  //   spv::StorageClass storage_class;
  //   std::vector<uint32_t> members;
  // };

  // std::vector<llvm::Type *> llvm_types(idbound);
  // std::vector<llvm::Value *> llvm_values(idbound);

  std::map<uint32_t, std::string> names;

  // std::set<uint32_t> blocks;

  std::vector<const uint32_t *> entries;

  // auto getname = [&names](uint32_t id) -> std::string {
  //   std::string name = names[id];
  //   if (name.empty())
  //     name = "spv__" + std::to_string(id);
  //   return name;
  // };
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
    switch (opcode) {
    case spv::OpName: {
      names[pCode[1]] = (const char *)&pCode[2];
      break;
    }
    case spv::OpExtInstImport: {
      break;
    }
    case spv::OpEntryPoint: {
      // we'll do this in a final pass once we have the function resolved
      entries.push_back(pCode);
      break;
    }
    case spv::OpMemberDecorate: {
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
    case spv::OpDecorate: {
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
    case spv::OpTypeVoid: {
      // llvm_types[word1] = Type::getVoidTy(c);
      primitive_types[word1] =
          PrimitiveTy{.id = word1, .type = Primitive_t::Void};
      break;
    }
    case spv::OpTypeFloat: {
      if (word2 == 16)
        primitive_types[word1] =
            PrimitiveTy{.id = word1, .type = Primitive_t::F16};
      // llvm_types[word1] = Type::getHalfTy(c);
      else if (word2 == 32)
        primitive_types[word1] =
            PrimitiveTy{.id = word1, .type = Primitive_t::F32};
      // llvm_types[word1] = Type::getFloatTy(c);
      else if (word2 == 64)
        primitive_types[word1] =
            PrimitiveTy{.id = word1, .type = Primitive_t::F64};
      // llvm_types[word1] = Type::getDoubleTy(c);
      else {
        UNIMPLEMENTED;
      }
      break;
    }
    case spv::OpTypeBool: {
      primitive_types[word1] =
          PrimitiveTy{.id = word1, .type = Primitive_t::I1};
      // llvm_types[word1] = Type::getInt1Ty(c);
      break;
    }
    case spv::OpTypeInt: {
      bool sign = word3 != 0;
      if (word2 == 8)
        primitive_types[word1] = PrimitiveTy{
            .id = word1, .type = (sign ? Primitive_t::I8 : Primitive_t::U8)};
      else if (word2 == 16)
        primitive_types[word1] = PrimitiveTy{
            .id = word1, .type = (sign ? Primitive_t::I16 : Primitive_t::U16)};
      else if (word2 == 32)
        primitive_types[word1] = PrimitiveTy{
            .id = word1, .type = (sign ? Primitive_t::I32 : Primitive_t::U32)};
      else if (word2 == 64)
        primitive_types[word1] = PrimitiveTy{
            .id = word1, .type = (sign ? Primitive_t::I64 : Primitive_t::U64)};
      else {
        UNIMPLEMENTED;
      }
      // llvm_types[word1] = IntegerType::get(c, word2);
      break;
    }
    case spv::OpTypeVector: {
      VectorTy type;
      type.id = word1;
      type.member_id = word2;
      type.width = word3;
      vector_types[word1] = type;
      // llvm_types[word1] = VectorType::get(llvm_types[word2], word3);
      break;
    }
    case spv::OpTypeArray: {
      ArrayTy type;
      type.id = word1;
      type.member_id = word2;
      type.width_id = word3;
      array_types[word1] = type;
      // llvm_types[word1] = ArrayType::get(
      //     llvm_types[word2],
      //     ((ConstantInt *)llvm_types[word3])->getValue().getLimitedValue());
      break;
    }
    case spv::OpTypeMatrix: {
      // implement matrix as just array
      // llvm_types[word1] = ArrayType::get(llvm_types[word2], word3);
      MatrixTy type;
      type.id = word1;
      type.vector_id = word2;
      type.width = word3;
      matrix_types[word1] = type;
      break;
    }
    case spv::OpTypePointer: {
      // llvm_types[word1] = PointerType::get(llvm_types[word3], 0);

      // if the pointed type is a block and this is a uniform pointer, propagate
      // that to the pointer
      // if (blocks.find(word3) != blocks.end() &&
      //     (word2 == spv::StorageClassUniform ||
      //      word2 == spv::StorageClassPushConstant))
      //   blocks.insert(word1);
      PtrTy type;
      type.id = word1;
      type.storage_class = (spv::StorageClass)word2;
      type.target_id = word3;
      ptr_types[word1] = type;
      // ptrtypes[word1] = word3;
      break;
    }
    case spv::OpTypeStruct: {
      StructTy type;
      type.id = word1;
      // std::vector<Type *> members;
      for (uint16_t i = 2; i < WordCount; i++)
        type.member_types.push_back(pCode[i]);
      struct_types[word1] = type;
      //   members.push_back(llvm_types[pCode[i]]);
      // llvm_types[word1] = StructType::create(c, members, getname(word1));
      break;
    }
    case spv::OpTypeFunction: {
      FunTy &f = functypes[word1];
      for (uint16_t i = 3; i < WordCount; i++)
        f.params.push_back(pCode[i]);
      f.ret = word2;
      break;
    }
    case spv::OpTypeImage: {
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
                                  : spv::AccessQualifierMax;
      images[word1] = type;
      break;
    }
    case spv::OpTypeSampledImage: {
      Sampled_ImageTy type;
      type.id = word1;
      type.sampled_image = word2;
      combined_images[word1] = type;
      break;
    }
    case spv::OpTypeSampler: {
      samplers[word1] = SamplerTy{word1};
      break;
    }

    case spv::OpVariable: {
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
      break;
    }

    case spv::OpFunction: {
      ASSERT_ALWAYS(cur_function == 0);
      Function fun;
      fun.id = word2;
      fun.result_type = word1;
      fun.function_type = word4;
      fun.control = (spv::FunctionControlMask)word2;
      cur_function = word2;
      functions[word2] = fun;
      break;
    }

    case spv::OpFunctionEnd: {
      ASSERT_ALWAYS(cur_function != 0);
      cur_function = 0;
      break;
    }

#if 0
    case spv::OpConstant: {
      Type *t = types[pCode[1]];
      if (t->isFloatTy())
        values[pCode[2]] = ConstantFP::get(t, ((float *)pCode)[3]);
      else
        values[pCode[2]] = ConstantInt::get(t, word3);
      break;
    }
    case spv::OpConstantComposite: {
      Type *t = types[pCode[1]];
      assert(t->isVectorTy());
      std::vector<Constant *> members;
      for (uint16_t i = 3; i < WordCount; i++)
        members.push_back((Constant *)values[pCode[i]]);
      values[pCode[2]] = ConstantVector::get(members);
      break;
    }
    case spv::OpNop: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUndef: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSourceContinued: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSource: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSourceExtension: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpName: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMemberName: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpString: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLine: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpExtension: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpExtInstImport: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpExtInst: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMemoryModel: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEntryPoint: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpExecutionMode: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCapability: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeVoid: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeBool: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeInt: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeFloat: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeVector: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeMatrix: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeImage: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeSampler: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeSampledImage: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeArray: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeRuntimeArray: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeStruct: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeOpaque: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypePointer: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeFunction: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeEvent: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeDeviceEvent: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeReserveId: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeQueue: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypePipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeForwardPointer: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConstantTrue: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConstantFalse: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConstant: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConstantComposite: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConstantSampler: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConstantNull: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSpecConstantTrue: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSpecConstantFalse: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSpecConstant: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSpecConstantComposite: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSpecConstantOp: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFunction: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFunctionParameter: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFunctionEnd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFunctionCall: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpVariable: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageTexelPointer: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLoad: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpStore: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCopyMemory: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCopyMemorySized: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAccessChain: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpInBoundsAccessChain: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpPtrAccessChain: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpArrayLength: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGenericPtrMemSemantics: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpInBoundsPtrAccessChain: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDecorate: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMemberDecorate: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDecorationGroup: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupDecorate: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupMemberDecorate: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpVectorExtractDynamic: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpVectorInsertDynamic: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpVectorShuffle: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCompositeConstruct: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCompositeExtract: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCompositeInsert: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCopyObject: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTranspose: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSampledImage: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleDrefImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleDrefExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleProjImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleProjExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleProjDrefImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleProjDrefExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageFetch: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageGather: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageDrefGather: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageRead: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageWrite: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImage: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageQueryFormat: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageQueryOrder: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageQuerySizeLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageQuerySize: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageQueryLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageQueryLevels: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageQuerySamples: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConvertFToU: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConvertFToS: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConvertSToF: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConvertUToF: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUConvert: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSConvert: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFConvert: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpQuantizeToF16: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConvertPtrToU: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSatConvertSToU: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSatConvertUToS: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConvertUToPtr: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpPtrCastToGeneric: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGenericCastToPtr: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGenericCastToPtrExplicit: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitcast: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSNegate: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFNegate: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIAdd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFAdd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpISub: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFSub: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIMul: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFMul: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUDiv: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSDiv: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFDiv: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUMod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSRem: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSMod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFRem: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFMod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpVectorTimesScalar: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMatrixTimesScalar: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpVectorTimesMatrix: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMatrixTimesVector: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMatrixTimesMatrix: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpOuterProduct: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDot: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIAddCarry: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpISubBorrow: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUMulExtended: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSMulExtended: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAny: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAll: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIsNan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIsInf: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIsFinite: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIsNormal: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSignBitSet: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLessOrGreater: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpOrdered: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUnordered: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLogicalEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLogicalNotEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLogicalOr: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLogicalAnd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLogicalNot: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSelect: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpINotEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUGreaterThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSGreaterThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUGreaterThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSGreaterThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpULessThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSLessThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpULessThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSLessThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFOrdEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFUnordEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFOrdNotEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFUnordNotEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFOrdLessThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFUnordLessThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFOrdGreaterThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFUnordGreaterThan: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFOrdLessThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFUnordLessThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFOrdGreaterThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFUnordGreaterThanEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpShiftRightLogical: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpShiftRightArithmetic: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpShiftLeftLogical: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitwiseOr: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitwiseXor: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitwiseAnd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpNot: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitFieldInsert: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitFieldSExtract: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitFieldUExtract: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitReverse: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBitCount: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDPdx: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDPdy: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFwidth: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDPdxFine: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDPdyFine: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFwidthFine: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDPdxCoarse: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDPdyCoarse: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFwidthCoarse: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEmitVertex: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEndPrimitive: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEmitStreamVertex: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEndStreamPrimitive: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpControlBarrier: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMemoryBarrier: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicLoad: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicStore: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicExchange: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicCompareExchange: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicCompareExchangeWeak: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicIIncrement: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicIDecrement: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicIAdd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicISub: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicSMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicUMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicSMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicUMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicAnd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicOr: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicXor: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpPhi: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLoopMerge: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSelectionMerge: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLabel: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBranch: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBranchConditional: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSwitch: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpKill: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReturn: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReturnValue: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUnreachable: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLifetimeStart: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpLifetimeStop: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupAsyncCopy: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupWaitEvents: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupAll: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupAny: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupBroadcast: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupIAdd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupFAdd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupFMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupUMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupSMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupFMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupUMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupSMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReadPipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpWritePipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReservedReadPipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReservedWritePipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReserveReadPipePackets: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReserveWritePipePackets: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCommitReadPipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCommitWritePipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIsValidReserveId: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetNumPipePackets: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetMaxPipePackets: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupReserveReadPipePackets: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupReserveWritePipePackets: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupCommitReadPipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupCommitWritePipe: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEnqueueMarker: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEnqueueKernel: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetKernelNDrangeSubGroupCount: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetKernelNDrangeMaxSubGroupSize: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetKernelWorkGroupSize: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetKernelPreferredWorkGroupSizeMultiple: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpRetainEvent: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReleaseEvent: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCreateUserEvent: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIsValidEvent: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSetUserEventStatus: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCaptureEventProfilingInfo: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetDefaultQueue: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBuildNDRange: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleDrefImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleDrefExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleProjImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleProjExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleProjDrefImplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseSampleProjDrefExplicitLod: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseFetch: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseGather: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseDrefGather: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseTexelsResident: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpNoLine: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicFlagTestAndSet: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAtomicFlagClear: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSparseRead: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSizeOf: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypePipeStorage: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpConstantPipeStorage: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCreatePipeFromPipeStorage: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetKernelLocalSizeForSubgroupCount: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGetKernelMaxNumSubgroups: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeNamedBarrier: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpNamedBarrierInitialize: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMemoryNamedBarrier: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpModuleProcessed: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpExecutionModeId: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDecorateId: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformElect: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformAll: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformAny: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformAllEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBroadcast: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBroadcastFirst: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBallot: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformInverseBallot: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBallotBitExtract: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBallotBitCount: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBallotFindLSB: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBallotFindMSB: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformShuffle: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformShuffleXor: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformShuffleUp: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformShuffleDown: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformIAdd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformFAdd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformIMul: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformFMul: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformSMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformUMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformFMin: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformSMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformUMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformFMax: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBitwiseAnd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBitwiseOr: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformBitwiseXor: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformLogicalAnd: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformLogicalOr: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformLogicalXor: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformQuadBroadcast: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformQuadSwap: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCopyLogical: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpPtrEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpPtrNotEqual: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpPtrDiff: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupBallotKHR: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupFirstInvocationKHR: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAllKHR: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAnyKHR: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAllEqualKHR: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupReadInvocationKHR: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupIAddNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupFAddNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupFMinNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupUMinNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupSMinNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupFMaxNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupUMaxNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupSMaxNonUniformAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFragmentMaskFetchAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpFragmentFetchAMD: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReadClockKHR: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpImageSampleFootprintNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpGroupNonUniformPartitionNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpWritePackedPrimitiveIndices4x8NV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpReportIntersectionNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIgnoreIntersectionNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTerminateRayNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTraceNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAccelerationStructureNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpExecuteCallableNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeCooperativeMatrixNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCooperativeMatrixLoadNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCooperativeMatrixStoreNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCooperativeMatrixMulAddNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpCooperativeMatrixLengthNV: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpBeginInvocationInterlockEXT: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpEndInvocationInterlockEXT: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDemoteToHelperInvocationEXT: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIsHelperInvocationEXT: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupShuffleINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupShuffleDownINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupShuffleUpINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupShuffleXorINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupBlockReadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupBlockWriteINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupImageBlockReadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupImageBlockWriteINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupImageMediaBlockReadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupImageMediaBlockWriteINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUCountLeadingZerosINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUCountTrailingZerosINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAbsISubINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpAbsUSubINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIAddSatINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUAddSatINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIAverageINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUAverageINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIAverageRoundedINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUAverageRoundedINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpISubSatINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUSubSatINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpIMul32x16INTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpUMul32x16INTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDecorateString: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpDecorateStringGOOGLE: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMemberDecorateString: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMemberDecorateStringGOOGLE: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpVmeImageINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeVmeImageINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcImePayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcRefPayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcSicPayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcMcePayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcMceResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcImeResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcImeResultSingleReferenceStreamoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcImeResultDualReferenceStreamoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcImeSingleReferenceStreaminINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcImeDualReferenceStreaminINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcRefResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpTypeAvcSicResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultInterBaseMultiReferencePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetInterBaseMultiReferencePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultInterShapePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetInterShapePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultInterDirectionPenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetInterDirectionPenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultIntraLumaShapePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultInterMotionVectorCostTableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultHighPenaltyCostTableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultMediumPenaltyCostTableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultLowPenaltyCostTableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetMotionVectorCostFunctionINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultIntraLumaModePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultNonDcLumaIntraPenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetDefaultIntraChromaModeBasePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetAcOnlyHaarINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetSourceInterlacedFieldPolarityINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetSingleReferenceInterlacedFieldPolarityINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceSetDualReferenceInterlacedFieldPolaritiesINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceConvertToImePayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceConvertToImeResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceConvertToRefPayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceConvertToRefResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceConvertToSicPayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceConvertToSicResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetMotionVectorsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetInterDistortionsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetBestInterDistortionsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetInterMajorShapeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetInterMinorShapeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetInterDirectionsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetInterMotionVectorCountINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetInterReferenceIdsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcMceGetInterReferenceInterlacedFieldPolaritiesINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeInitializeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeSetSingleReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeSetDualReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeRefWindowSizeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeAdjustRefOffsetINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeConvertToMcePayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeSetMaxMotionVectorCountINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeSetUnidirectionalMixDisableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeSetEarlySearchTerminationThresholdINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeSetWeightedSadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithSingleReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithDualReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithDualReferenceStreaminINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithSingleReferenceStreamoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithDualReferenceStreamoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeEvaluateWithDualReferenceStreaminoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeConvertToMceResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeGetSingleReferenceStreaminINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeGetDualReferenceStreaminINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeStripSingleReferenceStreamoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeStripDualReferenceStreamoutINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::
        OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeMotionVectorsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::
        OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeDistortionsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::
        OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeReferenceIdsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::
        OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeMotionVectorsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::
        OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeDistortionsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::
        OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeReferenceIdsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeGetBorderReachedINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeGetTruncatedSearchIndicationINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeGetUnidirectionalEarlySearchTerminationINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeGetWeightingPatternMinimumMotionVectorINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcImeGetWeightingPatternMinimumDistortionINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcFmeInitializeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcBmeInitializeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefConvertToMcePayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefSetBidirectionalMixDisableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefSetBilinearFilterEnableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefEvaluateWithSingleReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefEvaluateWithDualReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefEvaluateWithMultiReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefEvaluateWithMultiReferenceInterlacedINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcRefConvertToMceResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicInitializeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicConfigureSkcINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicConfigureIpeLumaINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicConfigureIpeLumaChromaINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetMotionVectorMaskINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicConvertToMcePayloadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicSetIntraLumaShapePenaltyINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicSetIntraLumaModeCostFunctionINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicSetIntraChromaModeCostFunctionINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicSetBilinearFilterEnableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicSetSkcForwardTransformEnableINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicSetBlockBasedRawSkipSadINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicEvaluateIpeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicEvaluateWithSingleReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicEvaluateWithDualReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicEvaluateWithMultiReferenceINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicEvaluateWithMultiReferenceInterlacedINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicConvertToMceResultINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetIpeLumaShapeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetBestIpeLumaDistortionINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetBestIpeChromaDistortionINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetPackedIpeLumaModesINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetIpeChromaModeINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetPackedSkcLumaCountThresholdINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetPackedSkcLumaSumThresholdINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpSubgroupAvcSicGetInterRawSadsINTEL: {
      UNIMPLEMENTED;
      break;
    }
    case spv::OpMax: {
      UNIMPLEMENTED;
      break;
    }
#endif
    default:
      break;
    }
    // stop once we hit the functions
    if (opcode == spv::OpFunction)
      break;

    pCode += WordCount;
  }
  // spirv_cross::Parser parser((uint32_t*)&bytes[0], bytes.size() / 4);
  // parser.parse();
  // auto ir = parser.get_parsed_ir();
  // spirv_cross::CompilerLLVM cpp(ir);
  // std::cout << cpp.compile();
  return 0;
}
#endif // S2L_EXE
