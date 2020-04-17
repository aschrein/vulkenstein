#include "3rdparty/SPIRV/GLSL.std.450.h"
#include "3rdparty/SPIRV/spirv.hpp"
//#include <fstream>
//#include <iostream>
#include <map>
#include <set>

#define UTILS_IMPL
#include "utils.hpp"

#include "vk.hpp"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/TargetSelect.h"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
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

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llvm_stdlib_1.h"
#include "llvm_stdlib_4.h"
#include "llvm_stdlib_64.h"

#define DLL_EXPORT __attribute__((visibility("default")))
#define ATTR_USED __attribute__((used))

#define LOOKUP_FN(name)                                                        \
  llvm::Function *name = module->getFunction(#name);                           \
  ASSERT_ALWAYS(name != NULL);
#define LOOKUP_TY(name)                                                        \
  llvm::Type *name = module->getTypeByName(#name);                             \
  ASSERT_ALWAYS(name != NULL);

void llvm_fatal(void *user_data, const std::string &reason,
                bool gen_crash_diag) {
  fprintf(stderr, "[LLVM_FATAL] %s\n", reason.c_str());
  abort();
}

static void WARNING(char const *fmt, ...) {
  static char buf[0x100];
  va_list argptr;
  va_start(argptr, fmt);
  vsnprintf(buf, sizeof(buf), fmt, argptr);
  va_end(argptr);
  fprintf(stderr, "[WARNING] %s\n", buf);
}

void *read_file(const char *filename, size_t *size,
                Allocator *allocator = NULL) {
  if (allocator == NULL)
    allocator = Allocator::get_default();
  FILE *f = fopen(filename, "rb");
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  *size = (size_t)fsize;
  char *data = (char *)allocator->alloc((size_t)fsize);
  fread(data, 1, (size_t)fsize, f);
  fclose(f);
  return data;
}

struct Jitted_Shader {
  void init() {
    llvm::ExitOnError ExitOnErr;
    jit->getMainJITDylib().addGenerator(ExitOnErr(
        llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            jit->getDataLayout().getGlobalPrefix())));
#define LOOKUP(name)                                                           \
  symbols.name =                                                               \
      (typeof(symbols.name))ExitOnErr(jit->lookup(#name)).getAddress();
    LOOKUP(spv_main)
    LOOKUP(get_private_size)
    LOOKUP(get_input_count)
    LOOKUP(get_input_stride)
    LOOKUP(get_output_stride)
    LOOKUP(get_export_count)
    LOOKUP(get_output_count)
    LOOKUP(get_export_items)
    LOOKUP(get_input_slots)
    LOOKUP(get_output_slots)
    LOOKUP(get_subgroup_size)
#undef LOOKUP
    symbols.input_item_count = symbols.get_input_count();
    symbols.get_input_slots((uint32_t *)&symbols.input_slots[0]);
    symbols.output_item_count = symbols.get_output_count();
    symbols.input_stride = symbols.get_input_stride();
    symbols.output_stride = symbols.get_output_stride();
    symbols.subgroup_size = symbols.get_subgroup_size();
    symbols.get_output_slots((uint32_t *)&symbols.output_slots[0]);
    symbols.private_storage_size = symbols.get_private_size();
    symbols.export_count = symbols.get_export_count();
    symbols.get_export_items(&symbols.export_items[0]);
  }
  Shader_Symbols symbols;
  std::unique_ptr<llvm::orc::LLJIT> jit;
};

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
struct ConstantComposite {
  uint32_t id;
  uint32_t type;
  std::vector<uint32_t> components;
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
  bool is_builtin;
  std::vector<uint32_t> member_types;
  std::vector<uint32_t> member_offsets;
  // Apparently there could be stuff like that
  // out gl_PerVertex
  // {
  //    vec4 gl_Position;
  // };
  std::vector<spv::BuiltIn> member_builtins;
  uint32_t size;
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
struct FunctionParameter {
  uint32_t id;
  uint32_t type_id;
};
struct Function {
  uint32_t id;
  uint32_t result_type;
  spv::FunctionControlMask control;
  uint32_t function_type;
  std::vector<FunctionParameter> params;
};
enum class DeclTy {
  PrimitiveTy,
  Variable,
  Function,
  PtrTy,
  RuntimeArrayTy,
  VectorTy,
  Constant,
  ConstantComposite,
  ArrayTy,
  ImageTy,
  SamplerTy,
  Sampled_ImageTy,
  FunTy,
  MatrixTy,
  StructTy,
  Unknown
};
struct Entry {
  uint32_t id;
  spv::ExecutionModel execution_model;
  std::string name;
};
struct Parsed_Op {
  spv::Op op;
  std::vector<uint32_t> args;
};
#include "spv_dump.hpp"

struct Spirv_Builder {
  //////////////////////
  //     Options      //
  //////////////////////
  uint32_t opt_subgroup_size = 4;
  bool opt_debug_comments = false;
  //  bool opt_deinterleave_attributes = false;

  //////////////////////
  // Meta information //
  //////////////////////
  std::map<uint32_t, PrimitiveTy> primitive_types;
  std::map<uint32_t, Variable> variables;
  std::map<uint32_t, Function> functions;
  std::map<uint32_t, PtrTy> ptr_types;
  std::map<uint32_t, VectorTy> vector_types;
  std::map<uint32_t, Constant> constants;
  std::map<uint32_t, ConstantComposite> constants_composite;
  std::map<uint32_t, ArrayTy> array_types;
  std::map<uint32_t, ImageTy> images;
  std::map<uint32_t, SamplerTy> samplers;
  std::map<uint32_t, Sampled_ImageTy> combined_images;
  std::map<uint32_t, std::vector<Decoration>> decorations;
  std::map<uint32_t, std::vector<Member_Decoration>> member_decorations;
  std::map<uint32_t, FunTy> functypes;
  std::map<uint32_t, MatrixTy> matrix_types;
  std::map<uint32_t, StructTy> struct_types;
  std::map<uint32_t, size_t> type_sizes;
  // function_id -> [var_id...]
  std::map<uint32_t, std::vector<uint32_t>> local_variables;
  std::vector<uint32_t> global_variables;
  // function_id -> [inst*...]
  std::map<uint32_t, std::vector<uint32_t const *>> instructions;
  std::map<uint32_t, std::string> names;
  std::map<std::pair<uint32_t, uint32_t>, std::string> member_names;
  std::map<uint32_t, Entry> entries;
  // Declaration order pairs
  std::vector<std::pair<uint32_t, DeclTy>> decl_types;
  std::map<uint32_t, DeclTy> decl_types_table;
  // Offsets for private variables
  std::map<uint32_t, uint32_t> private_offsets;
  uint32_t private_storage_size = 0;
  // Offsets for input variables
  std::vector<uint32_t> input_sizes;
  std::vector<uint32_t> input_offsets;
  std::vector<VkFormat> input_formats;
  uint32_t input_storage_size = 0;
  // Offsets for ouput variables
  std::vector<uint32_t> output_sizes;
  std::vector<uint32_t> output_offsets;
  std::vector<VkFormat> output_formats;
  uint32_t output_storage_size = 0;
  // Lifetime must be long enough
  uint32_t const *code;
  size_t code_size;
  int ATTR_USED dump_spirv_module() const {
    FILE *file = fopen("shader_dump.spv", "wb");
    fwrite(code, 1, code_size * 4, file);
    fclose(file);
    return 0;
  }
  //////////////////////////////
  //          METHODS         //
  //////////////////////////////

  auto has_decoration(spv::Decoration spv_dec, uint32_t val_id) -> bool {
    if (!contains(decorations, val_id))
      return false;
    auto &decs = decorations[val_id];
    for (auto &dec : decs) {
      if (dec.type == spv_dec)
        return true;
    }
    return false;
  }
  auto find_decoration(spv::Decoration spv_dec, uint32_t val_id) -> Decoration {
    ASSERT_ALWAYS(contains(decorations, val_id));
    auto &decs = decorations[val_id];
    for (auto &dec : decs) {
      if (dec.type == spv_dec)
        return dec;
    }
    UNIMPLEMENTED;
  }
  auto has_member_decoration(spv::Decoration spv_dec, uint32_t type_id,
                             uint32_t member_id) -> bool {
    ASSERT_ALWAYS(contains(member_decorations, type_id));
    for (auto &item : member_decorations[type_id]) {
      if (item.type == spv_dec && item.member_id == member_id) {
        return true;
      }
    }
    return false;
  }
  auto find_member_decoration(spv::Decoration spv_dec, uint32_t type_id,
                              uint32_t member_id) -> Member_Decoration {
    ASSERT_ALWAYS(contains(member_decorations, type_id));
    for (auto &item : member_decorations[type_id]) {
      if (item.type == spv_dec && item.member_id == member_id) {
        return item;
      }
    }
    UNIMPLEMENTED;
  }

  llvm::orc::ThreadSafeModule build_llvm_module_vectorized() {
    std::unique_ptr<llvm::LLVMContext> context(new llvm::LLVMContext());
    auto &c = *context;
    llvm::SMDiagnostic error;
    std::unique_ptr<llvm::Module> module = NULL;
    // Load the stdlib for a given subgroup size
    switch (opt_subgroup_size) {
    case 1: {
      auto mbuf = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef((char *)llvm_stdlib_1_bc, llvm_stdlib_1_bc_len), "",
          false);
      module = llvm::parseIR(*mbuf.get(), error, c);
      break;
    }
    case 4: {
      auto mbuf = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef((char *)llvm_stdlib_4_bc, llvm_stdlib_4_bc_len), "",
          false);
      module = llvm::parseIR(*mbuf.get(), error, c);
      break;
    }
    case 64: {
      auto mbuf = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef((char *)llvm_stdlib_64_bc, llvm_stdlib_64_bc_len), "",
          false);
      module = llvm::parseIR(*mbuf.get(), error, c);
      break;
    }
    default:
      UNIMPLEMENTED;
    };
    ASSERT_ALWAYS(module);

    llvm::install_fatal_error_handler(&llvm_fatal);
    auto llvm_get_constant_i32 = [&c](uint32_t a) {
      return llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(c), a);
    };
    auto llvm_get_constant_i64 = [&c](uint64_t a) {
      return llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(c), a);
    };
    const uint32_t *pCode = this->code;
    // The maximal number of IDs in this module
    const uint32_t ID_bound = pCode[3];
    auto get_spv_name = [this](uint32_t id) -> std::string {
      if (names.find(id) == names.end()) {
        names[id] = "spv_" + std::to_string(id);
      }
      ASSERT_ALWAYS(names.find(id) != names.end());
      return "spv_" + names[id];
    };
    auto llvm_matrix_transpose = [&c](llvm::Value *matrix,
                                      llvm::IRBuilder<> *llvm_builder) {
      llvm::Type *elem_type =
          matrix->getType()->getArrayElementType()->getVectorElementType();
      uint32_t matrix_row_size =
          matrix->getType()->getArrayElementType()->getVectorNumElements();
      uint32_t matrix_col_size =
          (uint32_t)matrix->getType()->getArrayNumElements();
      llvm::Type *matrix_t_type = llvm::ArrayType::get(
          llvm::VectorType::get(elem_type, matrix_col_size), matrix_row_size);
      llvm::Value *result = llvm::UndefValue::get(matrix_t_type);
      llvm::SmallVector<llvm::Value *, 4> rows;
      jto(matrix_row_size) {
        rows.push_back(llvm_builder->CreateExtractValue(matrix, j));
      }
      ito(matrix_col_size) {
        llvm::Value *result_row =
            llvm::UndefValue::get(matrix_t_type->getArrayElementType());
        jto(matrix_row_size) {
          result_row = llvm_builder->CreateInsertElement(
              result_row, llvm_builder->CreateExtractElement(rows[j], i), j);
        }
        result = llvm_builder->CreateInsertValue(result, result_row, i);
      }
      return result;
    };
    auto llvm_dot = [&c](llvm::Value *vector_0, llvm::Value *vector_1,
                         llvm::IRBuilder<> *llvm_builder) {
      llvm::Type *elem_type = vector_0->getType()->getVectorElementType();
      llvm::Value *result = llvm::ConstantFP::get(elem_type, 0.0);
      uint32_t vector_size = vector_0->getType()->getVectorNumElements();
      ASSERT_ALWAYS(vector_1->getType()->getVectorNumElements() ==
                    vector_0->getType()->getVectorNumElements());
      jto(vector_size) {
        result = llvm_builder->CreateFAdd(
            result, llvm_builder->CreateFMul(
                        llvm_builder->CreateExtractElement(vector_0, j),
                        llvm_builder->CreateExtractElement(vector_1, j)));
      }
      return result;
    };
    // Initialize framework functions

    LOOKUP_FN(get_push_constant_ptr);
    LOOKUP_FN(get_uniform_ptr);
    LOOKUP_FN(get_private_ptr);
    LOOKUP_FN(get_storage_ptr);
    LOOKUP_FN(get_uniform_const_ptr);
    LOOKUP_FN(get_input_ptr);
    // Pointer to user attributes
    LOOKUP_FN(get_output_ptr);
    // For vertex shaders et al holds a pointer to gl_Position
    LOOKUP_FN(get_builtin_output_ptr);
    LOOKUP_FN(kill);
    LOOKUP_FN(get_barycentrics);
    LOOKUP_FN(get_derivatives);
    LOOKUP_FN(dump_float4x4);
    LOOKUP_FN(dump_float4);
    LOOKUP_FN(dump_string);
    LOOKUP_FN(normalize_f2);
    LOOKUP_FN(normalize_f3);
    LOOKUP_FN(normalize_f4);
    LOOKUP_FN(get_combined_image);
    LOOKUP_FN(get_combined_sampler);
    LOOKUP_FN(spv_image_sample_2d_float4);
    LOOKUP_FN(spv_length_f2);
    LOOKUP_FN(spv_length_f3);
    LOOKUP_FN(spv_length_f4);
    LOOKUP_FN(spv_cross);
    LOOKUP_FN(spv_reflect);
    LOOKUP_FN(spv_pow);
    LOOKUP_FN(spv_clamp_f32);
    LOOKUP_FN(dummy_sample);
    LOOKUP_FN(spv_sqrt);
    LOOKUP_FN(spv_dot_f2);
    LOOKUP_FN(spv_dot_f3);
    LOOKUP_FN(spv_dot_f4);
    LOOKUP_FN(spv_get_global_invocation_id);
    LOOKUP_FN(spv_get_work_group_size);
    LOOKUP_FN(spv_lsb_i64);
    LOOKUP_FN(spv_atomic_add_i32);
    LOOKUP_FN(spv_atomic_sub_i32);
    LOOKUP_FN(spv_atomic_or_i32);
    std::map<std::string, llvm::GlobalVariable *> global_strings;
    auto lookup_string = [&](std::string str) {
      if (contains(global_strings, str))
        return global_strings[str];
      llvm::Constant *msg =
          llvm::ConstantDataArray::getString(c, str.c_str(), true);
      llvm::GlobalVariable *msg_glob =
          new llvm::GlobalVariable(*module, msg->getType(), true,
                                   llvm::GlobalValue::InternalLinkage, msg);
      global_strings[str] = msg_glob;
      return msg_glob;
    };
    auto lookup_image_op = [&](llvm::Type *res_type, llvm::Type *coord_type,
                               bool read) {
      static char tmp_buf[0x100];
      char const *type_names[] = {
          // clang-format off
        "invalid",  "invalid" ,
        "i32",      "f32"     ,
        "int2",     "float2"  ,
        "int3",     "float3"  ,
        "int3",     "float4"  ,
          // clang-format on
      };
      uint32_t dim = 0;
      uint32_t components = 0;
      llvm::Type *llvm_res_type = res_type;
      if (res_type->isVectorTy()) {
        components = llvm_res_type->getVectorNumElements();
        llvm_res_type = llvm_res_type->getVectorElementType();
      } else {
        components = 1;
      }
      if (coord_type->isVectorTy()) {
        dim = coord_type->getVectorNumElements();
      } else {
        dim = 1;
      }
      Primitive_t component_type = Primitive_t::Void;
      if (llvm_res_type->isFloatTy()) {
        component_type = Primitive_t::F32;
      } else if (llvm_res_type->isIntegerTy()) {
        ASSERT_ALWAYS(llvm_res_type->getIntegerBitWidth() == 32);
        component_type = Primitive_t::U32;
      } else {
        UNIMPLEMENTED;
      }

      ASSERT_ALWAYS(dim == 1 || dim == 2 || dim == 3 || dim == 4);
      ASSERT_ALWAYS(component_type == Primitive_t::F32 ||
                    component_type == Primitive_t::U32);
      uint32_t is_float = component_type == Primitive_t::F32 ? 1 : 0;
      char const *type_str = type_names[components * 2 + is_float];
      snprintf(tmp_buf, sizeof(tmp_buf), "spv_image_%s_%id_%s",
               (read ? "read" : "write"), dim, type_str);
      llvm::Function *fun = module->getFunction(tmp_buf);
      ASSERT_ALWAYS(fun != NULL);
      return fun;
    };
    llvm::Type *state_t = module->getTypeByName("struct.Invocation_Info");
    // Force 64 bit pointers
    llvm::Type *llvm_int_ptr_t = llvm::Type::getInt64Ty(c);
    llvm::Type *state_t_ptr = llvm::PointerType::get(state_t, 0);
    llvm::Type *mask_t = llvm::VectorType::get(llvm::IntegerType::getInt1Ty(c),
                                               opt_subgroup_size);

    llvm::Type *sampler_t = llvm::IntegerType::getInt64Ty(c);
    llvm::Type *image_t = llvm::IntegerType::getInt64Ty(c);
    llvm::Type *combined_image_t = llvm::IntegerType::getInt64Ty(c);

    // Structure member offsets for GEP
    // SPIRV has ways of setting the member offset
    // So in LLVM IR we need to manually insert paddings
    std::map<llvm::Type *, std::vector<uint32_t>> member_reloc;
    // Matrices could be row/column. here we just default to row major and do
    // the trasnpose as needed
    std::map<llvm::Type *, std::set<uint32_t>> member_transpose;
    // Global type table
    std::vector<llvm::Type *> llvm_types(ID_bound);
    // Global value table (constants and global values)
    // Each function creates a copy that inherits the global
    // table
    std::vector<llvm::Value *> llvm_global_values(ID_bound);
    auto get_global_const_i32 = [&llvm_global_values](uint32_t const_id) {
      llvm::ConstantInt *co =
          llvm::dyn_cast<llvm::ConstantInt>(llvm_global_values[const_id]);
      NOTNULL(co);
      return (uint32_t)co->getLimitedValue();
    };
    //    auto wave_local_t = [&](llvm::Type *ty) {
    //      return llvm::ArrayType::get(ty, opt_subgroup_size);
    //    };

    // Map spirv types to llvm types
    char name_buf[0x100];
    for (auto &item : this->decl_types) {
      ASSERT_ALWAYS(llvm_types[item.first] == NULL &&
                    "Types must have unique ids");
      ASSERT_ALWAYS(llvm_global_values[item.first] == NULL &&
                    "Values must have unique ids");
#define ASSERT_HAS(table) ASSERT_ALWAYS(table.find(item.first) != table.end());
      // Skip this declaration in this pass
      bool skip = false;
      switch (item.second) {
      case DeclTy::FunTy: {
        ASSERT_HAS(functypes);
        FunTy type = functypes.find(item.first)->second;
        llvm::Type *ret_type = llvm_types[type.ret];
        ASSERT_ALWAYS(ret_type != NULL &&
                      "Function must have a return type defined");
        llvm::SmallVector<llvm::Type *, 16> args;
        // Prepend state_t* to each function type
        args.push_back(state_t_ptr);
        // Prepend mask to each function type
        args.push_back(mask_t);
        for (auto &param_id : type.params) {
          llvm::Type *arg_type = llvm_types[param_id];
          ASSERT_ALWAYS(arg_type != NULL &&
                        "Function must have all argumet types defined");
          kto(opt_subgroup_size) args.push_back(arg_type);
        }
        if (ret_type->isVoidTy())
          llvm_types[type.id] = llvm::FunctionType::get(ret_type, args, false);
        else
          llvm_types[type.id] = llvm::FunctionType::get(
              llvm::ArrayType::get(ret_type, opt_subgroup_size), args, false);
        break;
      }
      case DeclTy::Function: {
        ASSERT_HAS(functions);
        Function fun = functions.find(item.first)->second;
        llvm::FunctionType *fun_type =
            llvm::dyn_cast<llvm::FunctionType>(llvm_types[fun.function_type]);
        ASSERT_ALWAYS(fun_type != NULL && "Function type must be defined");
        llvm_global_values[fun.id] =
            llvm::Function::Create(fun_type, llvm::GlobalValue::ExternalLinkage,
                                   get_spv_name(fun.id), module.get());
        break;
      }
      case DeclTy::RuntimeArrayTy:
      case DeclTy::PtrTy: {
        ASSERT_HAS(ptr_types);
        PtrTy type = ptr_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.target_id];
        ASSERT_ALWAYS(elem_t != NULL && "Pointer target type must be defined");
        llvm_types[type.id] = llvm::PointerType::get(elem_t, 0);
        break;
      }
      case DeclTy::ArrayTy: {
        ASSERT_HAS(array_types);
        ArrayTy type = array_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.member_id];
        ASSERT_ALWAYS(elem_t != NULL && "Element type must be defined");
        llvm::Value *width_value = llvm_global_values[type.width_id];
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
        ASSERT_HAS(images);
        ImageTy type = images.find(item.first)->second;
        llvm_types[type.id] = image_t;
        break;
      }
      case DeclTy::Constant: {
        ASSERT_HAS(constants);
        Constant c = constants.find(item.first)->second;
        llvm::Type *type = llvm_types[c.type];
        ASSERT_ALWAYS(type != NULL && "Constant type must be defined");
        // In LLVM float means float 32bit
        if (!(type->isFloatTy()) &&
            !(type->isIntegerTy() && (type->getIntegerBitWidth() == 32 ||
                                      type->getIntegerBitWidth() == 1))) {
          UNIMPLEMENTED;
        }
        if (type->isFloatTy())
          llvm_global_values[c.id] =
              llvm::ConstantFP::get(type, llvm::APFloat(c.f32_val));
        else {
          llvm_global_values[c.id] = llvm::ConstantInt::get(type, c.i32_val);
        }
        break;
      }
      case DeclTy::ConstantComposite: {
        ASSERT_HAS(constants_composite);
        ConstantComposite c = constants_composite.find(item.first)->second;
        llvm::Type *type = llvm_types[c.type];
        ASSERT_ALWAYS(type != NULL && "Constant type must be defined");
        llvm::SmallVector<llvm::Constant *, 4> llvm_elems;
        ito(c.components.size()) {
          llvm::Value *val = llvm_global_values[c.components[i]];
          llvm::Constant *cnst = llvm::dyn_cast<llvm::Constant>(val);
          ASSERT_ALWAYS(cnst != NULL);
          llvm_elems.push_back(cnst);
        }
        if (type->isVectorTy())
          llvm_global_values[c.id] = llvm::ConstantVector::get(llvm_elems);
        else if (type->isArrayTy())
          llvm_global_values[c.id] = llvm::ConstantArray::get(
              llvm::dyn_cast<llvm::ArrayType>(type), llvm_elems);
        else {
          UNIMPLEMENTED;
        }
        break;
      }
      case DeclTy::Variable: {
        ASSERT_HAS(variables);
        Variable c = variables.find(item.first)->second;
        switch (c.storage) {
        // Uniform data visible to all invocations
        case spv::StorageClass::StorageClassUniform:
        // Same as uniform but read-only and may hava an initializer
        case spv::StorageClass::StorageClassUniformConstant:
        // Pipeline ouput
        case spv::StorageClass::StorageClassOutput:
        // Pipeline input
        case spv::StorageClass::StorageClassInput:
        // Global memory visible to the current invocation
        case spv::StorageClass::StorageClassPrivate:
        // Global memory visible to the current invocation
        case spv::StorageClass::StorageClassStorageBuffer:
        // Small chunck of data visible to anyone
        case spv::StorageClass::StorageClassPushConstant: {
          // Handle global variables per function
          skip = true;
          break;
        }
        case spv::StorageClass::StorageClassFunction: {
          // Handle local variables at later passes per function
          skip = true;
          break;
        }
        default:
          UNIMPLEMENTED_(get_cstr(c.storage));
        }
        break;
      }
      case DeclTy::MatrixTy: {
        ASSERT_HAS(matrix_types);
        MatrixTy type = matrix_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.vector_id];
        ASSERT_ALWAYS(elem_t != NULL && "Matrix column type must be defined");
        llvm_types[type.id] = llvm::ArrayType::get(elem_t, type.width);
        break;
      }
      case DeclTy::StructTy: {
        ASSERT_HAS(struct_types);
        StructTy type = struct_types.find(item.first)->second;
        // For builtin structures we just emit struct <{<4 x float>}> for
        // gl_Position and assume that other members are not written to
        // @TODO: Implement other output structures
        if (type.is_builtin) {
          llvm::Type *struct_type = llvm::StructType::create(
              c, {llvm::VectorType::get(llvm::Type::getFloatTy(c), 4)},
              get_spv_name(type.id), true);
          member_reloc[struct_type] = {0};
          llvm_types[type.id] = struct_type;
          break;
        }
        std::vector<llvm::Type *> members;
        size_t offset = 0;
        // We manually insert padding bytes which offsets the structure members
        // for GEP instructions
        std::vector<uint32_t> member_indices;
        std::set<uint32_t> this_member_transpose;
        uint32_t index_offset = 0;
        for (uint32_t member_id = 0; member_id < type.member_types.size();
             member_id++) {
          llvm::Type *member_type = llvm_types[type.member_types[member_id]];
          ASSERT_ALWAYS(member_type != NULL && "Member types must be defined");
          if (!type.is_builtin && type.member_offsets[member_id] != offset) {
            ASSERT_ALWAYS(type.member_offsets[member_id] > offset &&
                          "Can't move a member back in memory layout");
            size_t diff = type.member_offsets[member_id] - offset;
            // Push dummy bytes until the member offset is ok
            members.push_back(
                llvm::ArrayType::get(llvm::Type::getInt8Ty(c), diff));
            index_offset += 1;
          }
          if (has_member_decoration(spv::Decoration::DecorationColMajor,
                                    type.id, member_id)) {
            this_member_transpose.insert(member_id);
            if (has_member_decoration(spv::Decoration::DecorationMatrixStride,
                                      type.id, member_id)) {
              // do we need to change anything if that's not true?
              // TODO allow 12 and 8 for mat3 and mat2
              ASSERT_ALWAYS(find_member_decoration(
                                spv::Decoration::DecorationMatrixStride,
                                type.id, member_id)
                                .param1 == 16);
            }
          }
          size_t size = 0;
          uint32_t member_type_id = type.member_types[member_id];
          member_indices.push_back(index_offset);
          index_offset += 1;
          ASSERT_ALWAYS(contains(type_sizes, member_type_id));
          size = type_sizes[member_type_id];
          ASSERT_ALWAYS(size != 0);
          offset += size;
          members.push_back(member_type);
        }
        llvm::Type *struct_type =
            llvm::StructType::create(c, members, get_spv_name(type.id), true);
        member_reloc[struct_type] = member_indices;
        if (this_member_transpose.size() != 0)
          member_transpose[struct_type] = this_member_transpose;
        llvm_types[type.id] = struct_type;
        break;
      }
      case DeclTy::VectorTy: {
        ASSERT_HAS(vector_types);
        VectorTy type = vector_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.member_id];
        ASSERT_ALWAYS(elem_t != NULL && "Element type must be defined");
        llvm_types[type.id] = llvm::VectorType::get(elem_t, type.width);
        break;
      }
      case DeclTy::SamplerTy: {
        ASSERT_HAS(samplers);
        SamplerTy type = samplers.find(item.first)->second;
        llvm_types[type.id] = sampler_t;
        break;
      }
      case DeclTy::Sampled_ImageTy: {
        ASSERT_HAS(combined_images);
        Sampled_ImageTy type = combined_images.find(item.first)->second;
        llvm_types[type.id] = combined_image_t;
        break;
      }
      case DeclTy::PrimitiveTy: {
        ASSERT_HAS(primitive_types);
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
      if (skip)
        continue;
      ASSERT_ALWAYS((llvm_global_values[item.first] != NULL ||
                     llvm_types[item.first] != NULL) &&
                    "eh there must be a type or value at the end!");
    }

    // Second pass:
    // Emit instructions
    for (auto &item : instructions) {
      // Control flow specific tracking
      struct BranchCond {
        llvm::BasicBlock *bb;
        spv::Op op;
        uint32_t cond_id;
        uint32_t true_id;
        uint32_t false_id;
        uint32_t merge_id;
        int32_t continue_id;
      };
      struct DeferredStore {
        llvm::Value *dst_ptr;
        llvm::Value *value_ptr;
      };
      int32_t cur_merge_id = -1;
      int32_t cur_continue_id = -1;

      std::vector<BranchCond> deferred_branches;
      std::vector<std::pair<llvm::BasicBlock *, uint32_t>> deferred_jumps;
      std::vector<DeferredStore> deferred_stores;
      std::map<uint32_t, llvm::BasicBlock *> llvm_labels;
      ///////////////////////////////////////////////
      uint32_t func_id = item.first;
      bool is_entry = contains(entries, func_id);
      bool is_pixel_shader =
          is_entry && entries[func_id].execution_model ==
                          spv::ExecutionModel::ExecutionModelFragment;
      NOTNULL(llvm_global_values[func_id]);
      llvm::Function *cur_fun =
          llvm::dyn_cast<llvm::Function>(llvm_global_values[func_id]);
      NOTNULL(cur_fun);
      llvm::Value *state_ptr = cur_fun->getArg(0);
      state_ptr->setName("state_ptr");
      // Check that the first parameter is state_t*
      ASSERT_ALWAYS(state_ptr->getType() == state_t_ptr);
      // @llvm/allocas
      llvm::BasicBlock *cur_bb =
          llvm::BasicBlock::Create(c, "allocas", cur_fun, NULL);
      llvm::Value *cur_mask = cur_fun->getArg(1);
      NOTNULL(cur_mask);
      ASSERT_ALWAYS(cur_mask->getType() == mask_t);
      std::unique_ptr<llvm::IRBuilder<>> llvm_builder;
      llvm_builder.reset(new llvm::IRBuilder<>(cur_bb, llvm::ConstantFolder()));
      // TODO(aschrein) maybe do something more optimal?
      // Each lane has it's own value table(quick and dirty way but will help me
      // advance)
      std::vector<std::vector<llvm::Value *>> llvm_values_per_lane(
          opt_subgroup_size);
      // Copy global constants into each lane
      ito(opt_subgroup_size) llvm_values_per_lane[i] = copy(llvm_global_values);
      // Setup arguments
      uint32_t cur_param_id = 2;
      Function function = functions[func_id];
      for (auto &param : function.params) {
        uint32_t res_type_id = param.type_id;
        uint32_t res_id = param.id;
        kto(opt_subgroup_size) {
          llvm_values_per_lane[k][res_id] = cur_fun->getArg(cur_param_id++);
        }
      }

      // Call to get input/output/uniform data
      llvm::Value *input_ptr =
          llvm_builder->CreateCall(get_input_ptr, state_ptr);
      llvm::Value *output_ptr =
          llvm_builder->CreateCall(get_output_ptr, state_ptr);
      llvm::Value *builtin_output_ptr =
          llvm_builder->CreateCall(get_builtin_output_ptr, state_ptr);
      // @llvm/local_variables
      auto &locals = local_variables[func_id];
      for (auto &var_id : locals) {
        ASSERT_ALWAYS(variables.find(var_id) != variables.end());
        Variable var = variables.find(var_id)->second;
        ASSERT_ALWAYS(var.storage == spv::StorageClass::StorageClassFunction);
        llvm::Type *llvm_type = llvm_types[var.type_id];
        NOTNULL(llvm_type);

        // SPIRV declares local variables as pointers so we need to allocate
        // the actual storage for them on the stack
        llvm::PointerType *ptr_type =
            llvm::dyn_cast<llvm::PointerType>(llvm_type);
        NOTNULL(ptr_type);

        llvm::Type *pointee_type = ptr_type->getElementType();
        ito(opt_subgroup_size) {
          llvm::Value *llvm_value = llvm_builder->CreateAlloca(
              pointee_type, 0, NULL, get_spv_name(var.id));
          llvm_values_per_lane[i][var.id] = llvm_value;
        }
        if (var.init_id != 0) {
          UNIMPLEMENTED;
        }
      }

      // Make shadow variables for global state(push_constants, uniforms,
      // samplers etc)
      // @llvm/global_variables
      for (auto &var_id : global_variables) {
        ASSERT_ALWAYS(variables.find(var_id) != variables.end());
        Variable var = variables.find(var_id)->second;
        ASSERT_ALWAYS(var.storage != spv::StorageClass::StorageClassFunction);
        llvm::Type *llvm_type = llvm_types[var.type_id];
        ASSERT_ALWAYS(llvm_type != NULL);
        ASSERT_ALWAYS(llvm_type->isPointerTy());
        PtrTy ptr_type = ptr_types[var.type_id];
        DeclTy pointee_decl_ty = decl_types_table[ptr_type.target_id];

        bool is_builtin_struct = false;
        StructTy builtin_struct_ty = {};
        if (decl_types_table[var.type_id] == DeclTy::PtrTy) {
          PtrTy ptr_ty = ptr_types[var.type_id];
          if (decl_types_table[ptr_ty.target_id] == DeclTy::StructTy) {
            StructTy struct_ty = struct_types[ptr_ty.target_id];
            // This is a structure with builtin members so no need for location
            if (struct_ty.is_builtin) {
              is_builtin_struct = true;
              builtin_struct_ty = struct_ty;
            }
          }
        }
        // In case that's a vector type we'd try to deinterleave it(flatten into
        // one huge vector of floats/ints)
        uint32_t num_components = 0;
        Primitive_t component_ty = Primitive_t::Void;
        switch (pointee_decl_ty) {
        case DeclTy::PrimitiveTy: {
          PrimitiveTy pty = primitive_types[ptr_type.target_id];
          if (pty.type == Primitive_t::F32) {
            num_components = 1;
          }
          break;
        }
        case DeclTy::VectorTy: {
          VectorTy vec_ty = vector_types[ptr_type.target_id];
          ASSERT_ALWAYS(vec_ty.width <= 4);
          ASSERT_ALWAYS(contains(primitive_types, vec_ty.member_id));
          component_ty = primitive_types[vec_ty.member_id].type;
          num_components = vec_ty.width;
          break;
        }
        default:
          break;
        }
        // Handle each storage type differently
        switch (var.storage) {
        case spv::StorageClass::StorageClassPrivate: {
          ito(opt_subgroup_size) {
            llvm::Value *pc_ptr =
                llvm_builder->CreateCall(get_private_ptr, state_ptr);
            ASSERT_ALWAYS(contains(private_offsets, var_id));
            uint32_t offset =
                private_storage_size * i + private_offsets[var_id];
            llvm::Value *pc_ptr_offset = llvm_builder->CreateGEP(
                pc_ptr, llvm::ConstantInt::get(c, llvm::APInt(32, offset)));
            llvm::Value *llvm_value = llvm_builder->CreateBitCast(
                pc_ptr_offset, llvm_type, get_spv_name(var.id));
            llvm_values_per_lane[i][var.id] = llvm_value;
          }
          break;
        }
        case spv::StorageClass::StorageClassUniformConstant:
        case spv::StorageClass::StorageClassStorageBuffer:
        case spv::StorageClass::StorageClassUniform: {
          uint32_t set =
              find_decoration(spv::Decoration::DecorationDescriptorSet, var.id)
                  .param1;
          uint32_t binding =
              find_decoration(spv::Decoration::DecorationBinding, var.id)
                  .param1;
          ASSERT_ALWAYS(set >= 0);
          ASSERT_ALWAYS(binding >= 0);
          llvm::Value *pc_ptr = llvm_builder->CreateCall(

              var.storage == spv::StorageClass::StorageClassUniformConstant
                  ? get_uniform_const_ptr
                  : var.storage == spv::StorageClass::StorageClassUniform
                        ? get_uniform_ptr
                        : get_storage_ptr,
              {state_ptr, llvm_builder->getInt32((uint32_t)set),
               llvm_builder->getInt32((uint32_t)binding)});
          llvm::Value *llvm_value = llvm_builder->CreateBitCast(
              pc_ptr, llvm_type, get_spv_name(var.id));
          ito(opt_subgroup_size) {
            llvm_values_per_lane[i][var.id] = llvm_value;
          }
          break;
        }
        case spv::StorageClass::StorageClassOutput: {
          if (is_builtin_struct) {
            llvm::ArrayType *array_type = llvm::ArrayType::get(
                llvm_type->getPointerElementType(), opt_subgroup_size);
            llvm::Value *alloca = llvm_builder->CreateAlloca(array_type);
            DeferredStore ds;
            ds.dst_ptr = builtin_output_ptr;
            ds.value_ptr = alloca;
            deferred_stores.push_back(ds);
            ito(opt_subgroup_size) {
              llvm::Value *offset = llvm_builder->CreateGEP(
                  alloca, {llvm_get_constant_i32(0), llvm_get_constant_i32(i)},
                  get_spv_name(var.id));

              llvm_values_per_lane[i][var.id] = offset;
            }
          } else {
            uint32_t location =
                find_decoration(spv::Decoration::DecorationLocation, var.id)
                    .param1;
            ASSERT_ALWAYS(location >= 0);
            ASSERT_ALWAYS(llvm_type->isPointerTy());

            llvm::ArrayType *array_type = llvm::ArrayType::get(
                llvm_type->getPointerElementType(), opt_subgroup_size);
            llvm::Value *alloca = llvm_builder->CreateAlloca(array_type);
            DeferredStore ds;
            ds.dst_ptr = llvm_builder->CreateGEP(
                output_ptr, llvm_get_constant_i32(output_offsets[location] *
                                                  opt_subgroup_size));
            ds.value_ptr = alloca;
            deferred_stores.push_back(ds);
            ito(opt_subgroup_size) {
              llvm::Value *offset = llvm_builder->CreateGEP(
                  alloca, {llvm_get_constant_i32(0), llvm_get_constant_i32(i)},
                  get_spv_name(var.id));

              llvm_values_per_lane[i][var.id] = offset;
            }
          }
          break;
        }
        case spv::StorageClass::StorageClassInput: {
          // Builtin variable: special case
          if (has_decoration(spv::Decoration::DecorationBuiltIn, var.id)) {
            spv::BuiltIn builtin_id =
                (spv::BuiltIn)find_decoration(
                    spv::Decoration::DecorationBuiltIn, var.id)
                    .param1;
            switch (builtin_id) {
            case spv::BuiltIn::BuiltInGlobalInvocationId: {
              llvm::VectorType *gid_t =
                  llvm::VectorType::get(llvm::IntegerType::getInt32Ty(c), 3);
              ito(opt_subgroup_size) {
                llvm::Value *alloca = llvm_builder->CreateAlloca(
                    gid_t, NULL, get_spv_name(var.id));
                llvm::Value *gid = llvm_builder->CreateCall(
                    spv_get_global_invocation_id,
                    {state_ptr, llvm_get_constant_i32(i)});
                llvm_builder->CreateStore(gid, alloca);
                llvm_values_per_lane[i][var.id] = alloca;
              }

              break;
            }
            case spv::BuiltIn::BuiltInWorkgroupSize: {
              llvm::VectorType *gid_t =
                  llvm::VectorType::get(llvm::IntegerType::getInt32Ty(c), 3);
              llvm::Value *alloca =
                  llvm_builder->CreateAlloca(gid_t, NULL, get_spv_name(var.id));
              llvm::Value *gid = llvm_builder->CreateCall(
                  spv_get_work_group_size, {state_ptr});
              llvm_builder->CreateStore(gid, alloca);
              ito(opt_subgroup_size) {
                llvm_values_per_lane[i][var.id] = alloca;
              }
              break;
            }
            default:
              UNIMPLEMENTED_(get_cstr(builtin_id));
            }
            break;
          }
          // Don't emit pipeline input variables for local functions because for
          // pixel shaders we need to interpolate and we don't know if a
          // function is called from a pixel shader of vertex shader
          if (!is_entry)
            break;
          // Pipeline input
          ASSERT_ALWAYS(
              (pointee_decl_ty == DeclTy::PrimitiveTy &&
               primitive_types[ptr_type.target_id].type == Primitive_t::F32) ||
              (pointee_decl_ty == DeclTy::VectorTy &&
               vector_types[ptr_type.target_id].width <= 4) ||
              false);
          uint32_t location =
              find_decoration(spv::Decoration::DecorationLocation, var.id)
                  .param1;
          ASSERT_ALWAYS(location >= 0);
          // For now just stupid array of structures
          ito(opt_subgroup_size) {
            // For pixel shader we need to interpolate pipeline inputs
            if (is_pixel_shader) {
              llvm::Value *barycentrics = llvm_builder->CreateCall(
                  get_barycentrics, {state_ptr, llvm_get_constant_i32(i)});
              llvm::Value *b_0 =
                  llvm_builder->CreateExtractElement(barycentrics, (uint64_t)0);
              llvm::Value *b_1 =
                  llvm_builder->CreateExtractElement(barycentrics, (uint64_t)1);
              llvm::Value *b_2 =
                  llvm_builder->CreateExtractElement(barycentrics, (uint64_t)2);
              llvm::Value *gep_0 = llvm_builder->CreateGEP(
                  input_ptr,
                  llvm_get_constant_i32(input_storage_size * (i * 3 + 0) +
                                        input_offsets[location]));
              llvm::Value *gep_1 = llvm_builder->CreateGEP(
                  input_ptr,
                  llvm_get_constant_i32(input_storage_size * (i * 3 + 1) +
                                        input_offsets[location]));
              llvm::Value *gep_2 = llvm_builder->CreateGEP(
                  input_ptr,
                  llvm_get_constant_i32(input_storage_size * (i * 3 + 2) +
                                        input_offsets[location]));
              llvm::Value *bitcast_0 =
                  llvm_builder->CreateBitCast(gep_0, llvm_type);
              llvm::Value *bitcast_1 =
                  llvm_builder->CreateBitCast(gep_1, llvm_type);
              llvm::Value *bitcast_2 =
                  llvm_builder->CreateBitCast(gep_2, llvm_type);
              llvm::Value *val_0 = llvm_builder->CreateLoad(bitcast_0);
              llvm::Value *val_1 = llvm_builder->CreateLoad(bitcast_1);
              llvm::Value *val_2 = llvm_builder->CreateLoad(bitcast_2);
              // TODO: handle more types/flat interpolation later
              ASSERT_ALWAYS(val_0->getType()->isVectorTy() ||
                            val_0->getType()->isFloatTy());
              if (val_0->getType()->isVectorTy()) {
                b_0 = llvm_builder->CreateVectorSplat(
                    val_0->getType()->getVectorNumElements(), b_0);
                b_1 = llvm_builder->CreateVectorSplat(
                    val_0->getType()->getVectorNumElements(), b_1);
                b_2 = llvm_builder->CreateVectorSplat(
                    val_0->getType()->getVectorNumElements(), b_2);
              }
              val_0 = llvm_builder->CreateFMul(val_0, b_0);
              val_1 = llvm_builder->CreateFMul(val_1, b_1);
              val_2 = llvm_builder->CreateFMul(val_2, b_2);
              llvm::Value *final_val = llvm_builder->CreateFAdd(
                  val_0, llvm_builder->CreateFAdd(val_1, val_2));
              llvm::Value *alloca =
                  llvm_builder->CreateAlloca(final_val->getType());
              llvm_builder->CreateStore(final_val, alloca);
              llvm_values_per_lane[i][var.id] = alloca;
            } else { // Just load raw input
              llvm::Value *gep = llvm_builder->CreateGEP(
                  input_ptr, llvm_get_constant_i32(input_storage_size * i +
                                                   input_offsets[location]));
              llvm::Value *bitcast =
                  llvm_builder->CreateBitCast(gep, llvm_type);
              llvm_values_per_lane[i][var.id] = bitcast;
            }
          }
          break;
        }
        case spv::StorageClass::StorageClassPushConstant: {
          llvm::Value *pc_ptr =
              llvm_builder->CreateCall(get_push_constant_ptr, state_ptr);
          llvm::Value *llvm_value = llvm_builder->CreateBitCast(
              pc_ptr, llvm_type, get_spv_name(var.id));
          ito(opt_subgroup_size) {
            llvm_values_per_lane[i][var.id] = llvm_value;
          }
          break;
        }
        default:
          UNIMPLEMENTED_(get_cstr(var.storage));
        }

        if (var.init_id != 0) {
          UNIMPLEMENTED;
        }
      }

      for (uint32_t const *pCode : item.second) {
        uint16_t WordCount = pCode[0] >> spv::WordCountShift;
        spv::Op opcode = spv::Op(pCode[0] & spv::OpCodeMask);
        uint32_t word1 = pCode[1];
        uint32_t word2 = WordCount > 2 ? pCode[2] : 0;
        uint32_t word3 = WordCount > 3 ? pCode[3] : 0;
        uint32_t word4 = WordCount > 4 ? pCode[4] : 0;
        uint32_t word5 = WordCount > 5 ? pCode[5] : 0;
        uint32_t word6 = WordCount > 6 ? pCode[6] : 0;
        uint32_t word7 = WordCount > 7 ? pCode[7] : 0;
        uint32_t word8 = WordCount > 8 ? pCode[8] : 0;
        uint32_t word9 = WordCount > 9 ? pCode[9] : 0;
        if (cur_bb != NULL && opt_debug_comments) {
          static char str_buf[0x100];
          std::vector<llvm::Type *> AsmArgTypes;
          std::vector<llvm::Value *> AsmArgs;
          snprintf(str_buf, sizeof(str_buf), "%s: word1: %i word2: %i",
                   get_cstr((spv::Op)opcode), word1, word2);
          llvm::FunctionType *AsmFTy = llvm::FunctionType::get(
              llvm::Type::getVoidTy(c), AsmArgTypes, false);
          llvm::InlineAsm *IA = llvm::InlineAsm::get(AsmFTy, str_buf, "", true,
                                                     /* IsAlignStack */ false,
                                                     llvm::InlineAsm::AD_ATT);
          llvm::CallInst::Create(IA, AsmArgs, "", cur_bb);
        }
        switch (opcode) {
        case spv::Op::OpLabel: {
          uint32_t id = word1;
          snprintf(name_buf, sizeof(name_buf), "label_%i", id);

          llvm::BasicBlock *new_bb =
              llvm::BasicBlock::Create(c, name_buf, cur_fun, NULL);
          if (cur_bb != NULL) {
            llvm::BranchInst::Create(new_bb, cur_bb);
          }
          cur_bb = new_bb;
          llvm_builder.reset(
              new llvm::IRBuilder<>(cur_bb, llvm::ConstantFolder()));
          llvm_labels[id] = cur_bb;
          break;
        }
        case spv::Op::OpAccessChain: {
          ASSERT_ALWAYS(cur_bb != NULL);
          kto(opt_subgroup_size) {
            llvm::Value *base = llvm_values_per_lane[k][word3];
            ASSERT_ALWAYS(base != NULL);

            std::vector<llvm::Value *> indices = {};

            for (uint32_t i = 4; i < WordCount; i++) {
              llvm::Value *index_val = llvm_values_per_lane[k][pCode[i]];
              ASSERT_ALWAYS(index_val != NULL);
              indices.push_back(index_val);
            }

            llvm::Type *result_type = llvm_types[word1];
            NOTNULL(result_type);
            llvm::Value *val = base;
            ito(indices.size()) {
              llvm::Value *index_val = indices[i];
              NOTNULL(index_val);
              llvm::Type *pointee_type =
                  val->getType()->getPointerElementType();
              NOTNULL(pointee_type);
              if (pointee_type->isStructTy()) {
                llvm::StructType *struct_type =
                    llvm::dyn_cast<llvm::StructType>(pointee_type);
                NOTNULL(struct_type);
                llvm::ConstantInt *integer =
                    llvm::dyn_cast<llvm::ConstantInt>(index_val);
                ASSERT_ALWAYS(
                    integer != NULL &&
                    "Access chain index must be OpConstant for structures");
                uint32_t cval = (uint32_t)integer->getLimitedValue();
                uint32_t struct_member_id = cval;
                bool transpose = false;
                if (contains(member_transpose, struct_type) &&
                    contains(member_transpose[struct_type], struct_member_id)) {
                  transpose = true;
                }
                if (contains(member_reloc, pointee_type)) {
                  std::vector<uint32_t> const &reloc_table =
                      member_reloc[pointee_type];
                  struct_member_id = reloc_table[cval];
                }
                llvm::Type *member_type =
                    struct_type->getElementType(struct_member_id);

                val = llvm_builder->CreateGEP(
                    val, {llvm::ConstantInt::get(llvm::Type::getInt32Ty(c),
                                                 (uint32_t)0),
                          llvm::ConstantInt::get(llvm::Type::getInt32Ty(c),
                                                 (uint32_t)struct_member_id)});
                if (transpose) {
                  llvm::Value *alloca = llvm_builder->CreateAlloca(member_type);
                  llvm_builder->CreateStore(
                      llvm_matrix_transpose(llvm_builder->CreateLoad(val),
                                            llvm_builder.get()),
                      alloca);
                  val = alloca;
                }
                // Make sure there is no reinterpretation at SPIRV level
                ASSERT_ALWAYS(i != indices.size() - 1 ||
                              result_type == val->getType());
              } else {
                if (i == indices.size() - 1) {
                  llvm::Value *reinter =
                      llvm_builder->CreateBitCast(val, result_type);
                  llvm::Value *gep =
                      llvm_builder->CreateGEP(reinter, index_val);
                  val = gep;
                } else {
                  llvm::Value *gep = llvm_builder->CreateGEP(val, index_val);
                  val = gep;
                }
              }
            }
            // SPIRV allows implicit reinterprets of pointers?
            llvm_values_per_lane[k][word2] = val;
          }
          break;
        }
        case spv::Op::OpLoad: {
          ASSERT_ALWAYS(cur_bb != NULL);
          kto(opt_subgroup_size) {
            llvm::Value *addr = llvm_values_per_lane[k][word3];
            ASSERT_ALWAYS(addr != NULL);
            llvm_values_per_lane[k][word2] = llvm_builder->CreateLoad(addr);
          }
          break;
        }
        case spv::Op::OpStore: {
          ASSERT_ALWAYS(cur_bb != NULL);
          kto(opt_subgroup_size) {
            llvm::Value *addr = llvm_values_per_lane[k][word1];
            ASSERT_ALWAYS(addr != NULL);
            llvm::Value *val = llvm_values_per_lane[k][word2];
            ASSERT_ALWAYS(val != NULL);
            llvm_builder->CreateStore(val, addr);
          }
          break;
        }
        // Skip structured control flow instructions for now
        case spv::Op::OpLoopMerge: {
          cur_merge_id = (int32_t)word1;
          cur_continue_id = (int32_t)word2;
          break;
        }
        case spv::Op::OpSelectionMerge: {
          cur_merge_id = (int32_t)word1;
          break;
        }
        case spv::Op::OpBranch: {
          // branches reference labels that haven't been created yet
          // So we just fix this up later
          deferred_jumps.push_back({cur_bb, word1});
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          break;
        }
        case spv::Op::OpBranchConditional: {
          ASSERT_ALWAYS(cur_merge_id >= 0);
          BranchCond bc;
          bc.op = opcode;
          bc.bb = cur_bb;
          bc.cond_id = word1;
          bc.true_id = word2;
          bc.false_id = word3;
          bc.merge_id = (uint32_t)cur_merge_id;
          bc.continue_id = cur_continue_id;
          // branches reference labels that haven't been created yet
          // So we just fix this up later
          deferred_branches.push_back(bc);
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          cur_merge_id = -1;
          cur_continue_id = -1;
          break;
        }
        case spv::Op::OpGroupNonUniformBallot: {
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t res_type_id = word1;
          uint32_t res_id = word2;
          uint32_t scope_id = word3;
          llvm::Value *scope_val = llvm_global_values[scope_id];
          llvm::ConstantInt *scope_const =
              llvm::dyn_cast<llvm::ConstantInt>(scope_val);
          NOTNULL(scope_const);
          uint32_t scope = (uint32_t)scope_const->getLimitedValue();
          uint32_t predicate_id = word4;
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Type *result_type = llvm_types[res_type_id];
          ASSERT_ALWAYS(result_type->isVectorTy() &&
                        result_type->getVectorNumElements() == 4 &&
                        result_type->getVectorElementType() ==
                            llvm::IntegerType::getInt32Ty(c));
          llvm::Value *result_value =
              llvm_builder->CreateVectorSplat(4, llvm_get_constant_i32(0));
          ASSERT_ALWAYS(opt_subgroup_size <= 64);
          llvm::Value *result_elem_0 = llvm_builder->CreateAnd(
              llvm_get_constant_i32(0), llvm_get_constant_i32(0));
          llvm::Value *result_elem_1 = llvm_builder->CreateAnd(
              llvm_get_constant_i32(0), llvm_get_constant_i32(0));
          kto(opt_subgroup_size) {
            llvm::Value *pred_val = llvm_values_per_lane[k][predicate_id];
            llvm::Value *lane_mask =
                llvm_builder->CreateExtractElement(cur_mask, k);
            llvm::Value *and_val = llvm_builder->CreateAnd(pred_val, lane_mask);
            llvm::Value *zext = llvm_builder->CreateZExt(
                and_val, llvm::IntegerType::getInt32Ty(c));
            if (k < 32) {
              llvm::Value *shift =
                  llvm_builder->CreateShl(zext, llvm::APInt(32, k));
              result_elem_0 = llvm_builder->CreateOr(shift, result_elem_0);
            } else {
              llvm::Value *shift =
                  llvm_builder->CreateShl(zext, llvm::APInt(32, k - 32));
              result_elem_1 = llvm_builder->CreateOr(shift, result_elem_1);
            }
          }
          result_value = llvm_builder->CreateInsertElement(
              result_value, result_elem_0, (uint64_t)0);
          result_value = llvm_builder->CreateInsertElement(
              result_value, result_elem_1, (uint64_t)1);
          kto(opt_subgroup_size) {
            llvm_values_per_lane[k][res_id] = result_value;
          }
          break;
        }
        case spv::Op::OpGroupNonUniformBallotBitCount: {
          ASSERT_ALWAYS(WordCount == 6);
          uint32_t res_type_id = word1;
          uint32_t res_id = word2;
          uint32_t scope_id = word3;
          uint32_t scope = get_global_const_i32(scope_id);
          uint32_t group_op = word4;
          uint32_t value_id = word5;
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          ASSERT_ALWAYS(result_type->isIntegerTy(32));
          llvm::SmallVector<llvm::Value *, 64> scan;
          scan.push_back(llvm_get_constant_i32(0));
          kto(opt_subgroup_size) {
            llvm::Value *val = llvm_values_per_lane[k][value_id];
            NOTNULL(val);
            ASSERT_ALWAYS(
                val->getType()->isVectorTy() &&
                val->getType()->getVectorNumElements() == 4 &&
                val->getType()->getVectorElementType()->isIntegerTy(32));
            llvm::Value *lane_mask_i32 = llvm_builder->CreateSExt(
                llvm_builder->CreateExtractElement(cur_mask, (uint64_t)k),
                llvm::Type::getInt32Ty(c));
            llvm::Value *popcnt_0 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (uint64_t)0)});
            llvm::Value *popcnt_1 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (uint64_t)1)});
            llvm::Value *popcnt_2 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (uint64_t)2)});
            llvm::Value *popcnt_3 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (uint64_t)3)});
            llvm::Value *popcnt = llvm_builder->CreateAdd(
                popcnt_0,
                llvm_builder->CreateAdd(
                    popcnt_1, llvm_builder->CreateAdd(popcnt_2, popcnt_3)));
            popcnt = llvm_builder->CreateAnd(lane_mask_i32, popcnt);
            scan.push_back(llvm_builder->CreateAdd(popcnt, scan.back()));
          }
          switch ((spv::GroupOperation)group_op) {
          case spv::GroupOperation::GroupOperationExclusiveScan: {
            kto(opt_subgroup_size) {
              llvm_values_per_lane[k][res_id] = scan[k];
            }
            break;
          }
          case spv::GroupOperation::GroupOperationInclusiveScan: {
            kto(opt_subgroup_size) {
              llvm_values_per_lane[k][res_id] = scan[k + 1];
            }
            break;
          }
          case spv::GroupOperation::GroupOperationReduce: {
            kto(opt_subgroup_size) {
              llvm_values_per_lane[k][res_id] = scan.back();
            }
            break;
          }
          default:
            UNIMPLEMENTED_(get_cstr((spv::GroupOperation)group_op));
          }
          break;
        }
        case spv::Op::OpGroupNonUniformElect: {
          ASSERT_ALWAYS(WordCount == 4);
          uint32_t res_type_id = word1;
          uint32_t res_id = word2;
          uint32_t scope_id = word3;
          uint32_t scope = get_global_const_i32(scope_id);
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Value *result = llvm_builder->CreateVectorSplat(
              opt_subgroup_size,
              llvm::Constant::getIntegerValue(llvm::Type::getInt1Ty(c),
                                              llvm::APInt(1, 0)));
          ASSERT_ALWAYS(opt_subgroup_size <= 64);
          llvm::Value *cur_mask_packed = llvm_builder->CreateBitCast(
              cur_mask, llvm::IntegerType::get(c, opt_subgroup_size));
          llvm::Value *cur_mask_i64 = llvm_builder->CreateZExt(
              cur_mask_packed, llvm::IntegerType::get(c, 64));
          llvm::Value *lsb =
              llvm_builder->CreateCall(spv_lsb_i64, cur_mask_i64);
          llvm::Value *election_mask =
              llvm_builder->CreateShl(llvm_get_constant_i64(1), lsb);
          kto(opt_subgroup_size) {
            // lane_bit = (i1)((election_mask >> k) & 1)
            llvm::Value *lane_bit = llvm_builder->CreateTrunc(
                llvm_builder->CreateAnd(
                    llvm_builder->CreateLShr(election_mask,
                                             llvm_get_constant_i64(k)),
                    llvm_get_constant_i64(1)),
                llvm::IntegerType::getInt1Ty(c));
            llvm_values_per_lane[k][res_id] = lane_bit;
          }
          break;
        }
        case spv::Op::OpGroupNonUniformBroadcastFirst: {
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t res_type_id = word1;
          uint32_t res_id = word2;
          uint32_t scope_id = word3;
          uint32_t value_id = word4;
          uint32_t scope = get_global_const_i32(scope_id);
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          llvm::ArrayType *result_arr =
              llvm::ArrayType::get(result_type, opt_subgroup_size);
          llvm::Value *result = llvm::UndefValue::get(result_arr);
          kto(opt_subgroup_size) {
            llvm::Value *lane_val = llvm_values_per_lane[k][value_id];
            result = llvm_builder->CreateInsertValue(result, lane_val,
                                                     {(uint32_t)k});
          }
          llvm::Value *cur_mask_packed = llvm_builder->CreateBitCast(
              cur_mask, llvm::IntegerType::get(c, opt_subgroup_size));
          llvm::Value *cur_mask_i64 = llvm_builder->CreateZExt(
              cur_mask_packed, llvm::IntegerType::get(c, 64));
          llvm::Value *lsb =
              llvm_builder->CreateCall(spv_lsb_i64, cur_mask_i64);
          llvm::Value *stack_proxy =
              llvm_builder->CreateAlloca(result->getType());
          llvm_builder->CreateStore(result, stack_proxy);
          llvm::Value *gep = llvm_builder->CreateGEP(
              stack_proxy, {llvm_get_constant_i32(0), lsb});
          llvm::Value *broadcast = llvm_builder->CreateLoad(gep);
          kto(opt_subgroup_size) {
            llvm_values_per_lane[k][res_id] = broadcast;
          }
          break;
        }
        case spv::Op::OpAtomicISub:
        case spv::Op::OpAtomicOr:
        case spv::Op::OpAtomicIAdd: {
          ASSERT_ALWAYS(WordCount == 7);
          uint32_t res_type_id = word1;
          uint32_t res_id = word2;
          uint32_t pointer_id = word3;
          uint32_t scope_id = word4;
          uint32_t semantics_id = word5;
          uint32_t value_id = word6;
          uint32_t scope = get_global_const_i32(scope_id);
          uint32_t semantics = get_global_const_i32(semantics_id);
          // TODO(aschrein): implement memory semantics
          //          spv::MemorySemanticsMask::MemorySemantics
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          ASSERT_ALWAYS(result_type->isIntegerTy() &&
                        result_type->getIntegerBitWidth() == 32);
          // Do they have different pointers?
          kto(opt_subgroup_size) {
            llvm::Value *val = llvm_values_per_lane[k][value_id];
            NOTNULL(val);
            ASSERT_ALWAYS(val->getType()->isIntegerTy() &&
                          val->getType()->getIntegerBitWidth() == 32);
            llvm::Value *ptr = llvm_values_per_lane[k][pointer_id];
            NOTNULL(ptr);
            ASSERT_ALWAYS(
                ptr->getType()->isPointerTy() &&
                ptr->getType()->getPointerElementType()->isIntegerTy() &&
                ptr->getType()->getPointerElementType()->getIntegerBitWidth() ==
                    32);
            llvm_values_per_lane[k][res_id] = llvm_builder->CreateCall(
                // clang-format off
                opcode == spv::Op::OpAtomicISub ? spv_atomic_sub_i32 :
                opcode == spv::Op::OpAtomicIAdd ? spv_atomic_add_i32 :
                opcode == spv::Op::OpAtomicOr   ? spv_atomic_or_i32 :
                                                  NULL
                // clang-format on
                ,
                {ptr, val});
          }
          break;
        }
        case spv::Op::OpPhi: {
          uint32_t res_type_id = word1;
          uint32_t res_id = word2;
          // (var, parent_bb)
          std::vector<std::pair<uint32_t, uint32_t>> vars;
          ASSERT_ALWAYS((WordCount - 3) % 2 == 0);
          ito((WordCount - 3) / 2) {
            vars.push_back({pCode[3 + i * 2], pCode[3 + i * 2 + 1]});
          }
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          kto(opt_subgroup_size) {
            llvm::PHINode *llvm_phi =
                llvm_builder->CreatePHI(result_type, (uint32_t)vars.size());
            ito(vars.size()) {
              llvm::Value *value = llvm_values_per_lane[k][vars[i].first];
              llvm::BasicBlock *parent_bb = llvm_labels[vars[i].second];
              NOTNULL(parent_bb);
              NOTNULL(value);
              llvm_phi->addIncoming(value, parent_bb);
            }
            llvm_values_per_lane[k][res_id] = llvm_phi;
          }
          break;
        }
#define SIMPLE_LLVM_OP(llvm_op)                                                \
  kto(opt_subgroup_size) {                                                     \
    ASSERT_ALWAYS(llvm_values_per_lane[k][word2] == NULL);                     \
    ASSERT_ALWAYS(llvm_values_per_lane[k][word3] != NULL);                     \
    ASSERT_ALWAYS(llvm_values_per_lane[k][word4] != NULL);                     \
    llvm_values_per_lane[k][word2] = llvm_builder->llvm_op(                    \
        llvm_values_per_lane[k][word3], llvm_values_per_lane[k][word4]);       \
  };
        case spv::Op::OpIEqual: {
          SIMPLE_LLVM_OP(CreateICmpEQ);
          break;
        }
        case spv::Op::OpINotEqual: {
          SIMPLE_LLVM_OP(CreateICmpNE);
          break;
        }
        case spv::Op::OpUGreaterThan: {
          SIMPLE_LLVM_OP(CreateICmpUGT);
          break;
        }
        case spv::Op::OpSGreaterThan: {
          SIMPLE_LLVM_OP(CreateICmpSGT);
          break;
        }
        case spv::Op::OpUGreaterThanEqual: {
          SIMPLE_LLVM_OP(CreateICmpUGE);
          break;
        }
        case spv::Op::OpSGreaterThanEqual: {
          SIMPLE_LLVM_OP(CreateICmpSGE);
          break;
        }
        case spv::Op::OpULessThan: {
          SIMPLE_LLVM_OP(CreateICmpULT);
          break;
        }
        case spv::Op::OpSLessThan: {
          SIMPLE_LLVM_OP(CreateICmpSLT);
          break;
        }
        case spv::Op::OpULessThanEqual: {
          SIMPLE_LLVM_OP(CreateICmpULE);
          break;
        }
        case spv::Op::OpSLessThanEqual: {
          SIMPLE_LLVM_OP(CreateICmpSLE);
          break;
        }
        case spv::Op::OpIAdd: {
          SIMPLE_LLVM_OP(CreateAdd);
          break;
        }
        case spv::Op::OpFAdd: {
          SIMPLE_LLVM_OP(CreateFAdd);
          break;
        }
        case spv::Op::OpISub: {
          SIMPLE_LLVM_OP(CreateSub);
          break;
        }
        case spv::Op::OpFSub: {
          SIMPLE_LLVM_OP(CreateFSub);
          break;
        }
        case spv::Op::OpIMul: {
          SIMPLE_LLVM_OP(CreateMul);
          break;
        }
        case spv::Op::OpFMul: {
          SIMPLE_LLVM_OP(CreateFMul);
          break;
        }
        case spv::Op::OpUDiv: {
          SIMPLE_LLVM_OP(CreateUDiv);
          break;
        }
        case spv::Op::OpSDiv: {
          SIMPLE_LLVM_OP(CreateSDiv);
          break;
        }
        case spv::Op::OpFDiv: {
          SIMPLE_LLVM_OP(CreateFDiv);
          break;
        }
        case spv::Op::OpUMod: {
          SIMPLE_LLVM_OP(CreateURem);
          break;
        }
        case spv::Op::OpSRem: {
          SIMPLE_LLVM_OP(CreateSRem);
          break;
        }
        case spv::Op::OpSMod: {
          // TODO(aschrein): srem != smod
          SIMPLE_LLVM_OP(CreateSRem);
          break;
        }
        case spv::Op::OpFRem: {
          SIMPLE_LLVM_OP(CreateFRem);
          break;
        }
        case spv::Op::OpFMod: {
          // TODO(aschrein): srem != smod
          SIMPLE_LLVM_OP(CreateFRem);
          break;
        }
        case spv::Op::OpLogicalEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpLogicalNotEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpLogicalOr: {
          SIMPLE_LLVM_OP(CreateOr);
          break;
        }
        case spv::Op::OpLogicalAnd: {
          SIMPLE_LLVM_OP(CreateAnd);
          break;
        }
        case spv::Op::OpSelect: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFOrdEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFUnordEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFOrdNotEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFUnordNotEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFOrdLessThan: {
          SIMPLE_LLVM_OP(CreateFCmpOLT);
          break;
        }
        case spv::Op::OpFUnordLessThan: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFOrdGreaterThan: {
          SIMPLE_LLVM_OP(CreateFCmpUGT);
          break;
        }
        case spv::Op::OpFUnordGreaterThan: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFOrdLessThanEqual: {
          SIMPLE_LLVM_OP(CreateFCmpOLE);
          break;
        }
        case spv::Op::OpFUnordLessThanEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFOrdGreaterThanEqual: {
          SIMPLE_LLVM_OP(CreateFCmpOGT);
          break;
        }
        case spv::Op::OpFUnordGreaterThanEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpShiftRightLogical: {
          SIMPLE_LLVM_OP(CreateLShr);
          break;
        }
        case spv::Op::OpShiftRightArithmetic: {
          SIMPLE_LLVM_OP(CreateAShr);
          break;
        }
        case spv::Op::OpShiftLeftLogical: {
          SIMPLE_LLVM_OP(CreateShl);
          break;
        }
        case spv::Op::OpBitwiseOr: {
          SIMPLE_LLVM_OP(CreateOr);
          break;
        }
        case spv::Op::OpBitwiseXor: {
          SIMPLE_LLVM_OP(CreateXor);
          break;
        }
        case spv::Op::OpBitwiseAnd: {
          SIMPLE_LLVM_OP(CreateAnd);
          break;
        }
        case spv::Op::OpNot: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
#undef SIMPLE_LLVM_OP
        case spv::Op::OpConvertUToF: {
          kto(opt_subgroup_size) {
            llvm::Type *dest_ty = llvm_types[word1];
            NOTNULL(dest_ty);
            llvm::Value *src_val = llvm_values_per_lane[k][word3];
            NOTNULL(src_val);
            llvm_values_per_lane[k][word2] =
                llvm_builder->CreateUIToFP(src_val, dest_ty);
          }
          break;
        }
        case spv::Op::OpLogicalNot: {
          kto(opt_subgroup_size) {
            llvm::Value *src_val = llvm_values_per_lane[k][word3];
            NOTNULL(src_val);
            llvm_values_per_lane[k][word2] = llvm_builder->CreateNot(src_val);
          }
          break;
        }
        case spv::Op::OpKill: {
          llvm_builder->CreateCall(
              kill, {state_ptr, llvm_get_constant_i32((uint32_t)~0)});
          break;
        }
        case spv::Op::OpVectorTimesScalar: {
          kto(opt_subgroup_size) {
            llvm::Value *vector = llvm_values_per_lane[k][word3];
            llvm::Value *scalar = llvm_values_per_lane[k][word4];
            ASSERT_ALWAYS(vector != NULL && scalar != NULL);
            llvm::VectorType *vtype =
                llvm::dyn_cast<llvm::VectorType>(vector->getType());
            ASSERT_ALWAYS(vtype != NULL);
            llvm::Value *splat = llvm_builder->CreateVectorSplat(
                vtype->getVectorNumElements(), scalar);
            llvm_values_per_lane[k][word2] =
                llvm_builder->CreateFMul(vector, splat);
          }
          break;
        }
        case spv::Op::OpCompositeExtract: {
          kto(opt_subgroup_size) {
            llvm::Value *src = llvm_values_per_lane[k][word3];
            llvm::Type *src_type = src->getType();
            ASSERT_ALWAYS(WordCount > 4);
            uint32_t indices_count = WordCount - 4;
            llvm::Value *val = src;
            ito(indices_count) {
              if (val->getType()->isArrayTy()) {
                val = llvm_builder->CreateExtractValue(val, pCode[i + 4]);
              } else if (val->getType()->isVectorTy()) {
                val = llvm_builder->CreateExtractElement(val, pCode[i + 4]);
              } else {
                UNIMPLEMENTED;
              }
              llvm_values_per_lane[k][word2] = val;
            }
          }
          break;
        }
        case spv::Op::OpCompositeConstruct: {
          kto(opt_subgroup_size) {
            llvm::Type *dst_type = llvm_types[word1];
            ASSERT_ALWAYS(dst_type != NULL);
            if (dst_type->isVectorTy()) {
              llvm::Value *undef = llvm::UndefValue::get(dst_type);
              llvm::VectorType *vtype =
                  llvm::dyn_cast<llvm::VectorType>(dst_type);
              ASSERT_ALWAYS(vtype != NULL);
              llvm::Value *final_val = undef;
              ito(vtype->getVectorNumElements()) {
                llvm::Value *src = llvm_values_per_lane[k][pCode[3 + i]];
                ASSERT_ALWAYS(src != NULL);
                final_val =
                    llvm_builder->CreateInsertElement(final_val, src, i);
              }
              llvm_values_per_lane[k][word2] = final_val;
            } else if (dst_type->isArrayTy()) {
              llvm::Value *undef = llvm::UndefValue::get(dst_type);
              llvm::ArrayType *atype =
                  llvm::dyn_cast<llvm::ArrayType>(dst_type);
              ASSERT_ALWAYS(atype != NULL);
              llvm::Value *final_val = undef;
              ito(atype->getArrayNumElements()) {
                llvm::Value *src = llvm_values_per_lane[k][pCode[3 + i]];
                ASSERT_ALWAYS(src != NULL);
                final_val = llvm_builder->CreateInsertValue(final_val, src, i);
              }
              llvm_values_per_lane[k][word2] = final_val;
            } else {
              UNIMPLEMENTED;
            }
          }
          break;
        }
        case spv::Op::OpVectorShuffle: {
          kto(opt_subgroup_size) {
            llvm::Value *op1 = llvm_values_per_lane[k][word3];
            llvm::Value *op2 = llvm_values_per_lane[k][word4];
            ASSERT_ALWAYS(op1 != NULL && op2 != NULL);
            llvm::VectorType *vtype1 =
                llvm::dyn_cast<llvm::VectorType>(op1->getType());
            llvm::VectorType *vtype2 =
                llvm::dyn_cast<llvm::VectorType>(op2->getType());
            ASSERT_ALWAYS(vtype1 != NULL && vtype2 != NULL);
            std::vector<uint32_t> indices;
            for (uint16_t i = 5; i < WordCount; i++)
              indices.push_back(pCode[i]);
            // In LLVM shufflevector must have both operands of the same type
            // In SPIRV operands may be of different types
            // Handle the different size case by creating one big vector and
            // construct the final vector by extracting elements from it
            if (vtype1->getVectorNumElements() !=
                vtype2->getVectorNumElements()) {
              uint32_t total_width = vtype1->getVectorNumElements() +
                                     vtype2->getVectorNumElements();
              llvm::VectorType *new_vtype = llvm::VectorType::get(
                  vtype1->getVectorElementType(), total_width);
              // Create a dummy super vector and appedn op1 and op2 elements
              // to it
              llvm::Value *prev = llvm::UndefValue::get(new_vtype);
              ito(vtype1->getVectorNumElements()) {
                llvm::Value *extr = llvm_builder->CreateExtractElement(op1, i);
                prev = llvm_builder->CreateInsertElement(prev, extr, i);
              }
              uint32_t offset = vtype1->getVectorNumElements();
              ito(vtype2->getVectorNumElements()) {
                llvm::Value *extr = llvm_builder->CreateExtractElement(op2, i);
                prev =
                    llvm_builder->CreateInsertElement(prev, extr, i + offset);
              }
              // Now we need to emit a chain of extact elements to make up the
              // result
              llvm::VectorType *res_type = llvm::VectorType::get(
                  vtype1->getVectorElementType(), (uint32_t)indices.size());
              llvm::Value *res = llvm::UndefValue::get(res_type);
              ito(indices.size()) {
                llvm::Value *elem =
                    llvm_builder->CreateExtractElement(prev, indices[i]);
                res = llvm_builder->CreateInsertElement(res, elem, i);
              }
              llvm_values_per_lane[k][word2] = res;
            } else {
              ASSERT_ALWAYS(llvm_builder && cur_bb != NULL);
              llvm_values_per_lane[k][word2] =
                  llvm_builder->CreateShuffleVector(op1, op2, indices);
            }
          }
          break;
        }
        case spv::Op::OpDot: {
          kto(opt_subgroup_size) {
            uint32_t res_id = word2;
            uint32_t op1_id = word3;
            uint32_t op2_id = word4;
            llvm::Value *op1_val = llvm_values_per_lane[k][op1_id];
            ASSERT_ALWAYS(op1_val != NULL);
            llvm::VectorType *op1_vtype =
                llvm::dyn_cast<llvm::VectorType>(op1_val->getType());
            ASSERT_ALWAYS(op1_vtype != NULL);
            llvm::Value *op2_val = llvm_values_per_lane[k][op2_id];
            ASSERT_ALWAYS(op2_val != NULL);
            llvm::VectorType *op2_vtype =
                llvm::dyn_cast<llvm::VectorType>(op2_val->getType());
            ASSERT_ALWAYS(op2_vtype != NULL);
            ASSERT_ALWAYS(op1_vtype->getVectorNumElements() ==
                          op2_vtype->getVectorNumElements());
            switch (op1_vtype->getVectorNumElements()) {
            case 2:
              llvm_values_per_lane[k][res_id] =
                  llvm_builder->CreateCall(spv_dot_f2, {op1_val, op2_val});
              break;
            case 3:
              llvm_values_per_lane[k][res_id] =
                  llvm_builder->CreateCall(spv_dot_f3, {op1_val, op2_val});
              break;
            case 4:
              llvm_values_per_lane[k][res_id] =
                  llvm_builder->CreateCall(spv_dot_f4, {op1_val, op2_val});
              break;
            default:
              UNIMPLEMENTED;
            }
          }
          break;
        }
        case spv::Op::OpExtInst: {
          uint32_t result_type_id = word1;
          uint32_t result_id = word2;
          uint32_t set_id = word3;
          spv::GLSLstd450 inst = (spv::GLSLstd450)word4;
          switch (inst) {
          case spv::GLSLstd450::GLSLstd450Normalize: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(arg != NULL);
              llvm::VectorType *vtype =
                  llvm::dyn_cast<llvm::VectorType>(arg->getType());
              ASSERT_ALWAYS(vtype != NULL);
              uint32_t width = vtype->getVectorNumElements();
              switch (width) {
              case 2:
                llvm_values_per_lane[k][result_id] =
                    llvm_builder->CreateCall(normalize_f2, {arg});
                break;
              case 3:
                llvm_values_per_lane[k][result_id] =
                    llvm_builder->CreateCall(normalize_f3, {arg});
                break;
              case 4:
                llvm_values_per_lane[k][result_id] =
                    llvm_builder->CreateCall(normalize_f4, {arg});
                break;
              default:
                UNIMPLEMENTED;
              }
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450Sqrt: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(arg != NULL);
              llvm::VectorType *vtype =
                  llvm::dyn_cast<llvm::VectorType>(arg->getType());
              if (vtype != NULL) {

                ASSERT_ALWAYS(vtype != NULL);
                uint32_t width = vtype->getVectorNumElements();
                llvm::Value *prev = arg;
                ito(width) {
                  llvm::Value *elem =
                      llvm_builder->CreateExtractElement(arg, i);
                  llvm::Value *sqrt =
                      llvm_builder->CreateCall(spv_sqrt, {elem});
                  prev = llvm_builder->CreateInsertElement(prev, sqrt, i);
                }
                llvm_values_per_lane[k][result_id] = prev;
              } else {
                llvm::Type *type = arg->getType();
                ASSERT_ALWAYS(type->isFloatTy());
                llvm_values_per_lane[k][result_id] =
                    llvm_builder->CreateCall(spv_sqrt, {arg});
              }
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450Length: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(arg != NULL);
              llvm::VectorType *vtype =
                  llvm::dyn_cast<llvm::VectorType>(arg->getType());
              ASSERT_ALWAYS(vtype != NULL);
              uint32_t width = vtype->getVectorNumElements();
              switch (width) {
              case 2:
                llvm_values_per_lane[k][result_id] =
                    llvm_builder->CreateCall(spv_length_f2, {arg});
                break;
              case 3:
                llvm_values_per_lane[k][result_id] =
                    llvm_builder->CreateCall(spv_length_f3, {arg});
                break;
              case 4:
                llvm_values_per_lane[k][result_id] =
                    llvm_builder->CreateCall(spv_length_f4, {arg});
                break;
              default:
                UNIMPLEMENTED;
              }
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450Cross: {
            ASSERT_ALWAYS(WordCount == 7);
            kto(opt_subgroup_size) {
              llvm::Value *op1 = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(op1 != NULL);
              llvm::Value *op2 = llvm_values_per_lane[k][pCode[6]];
              ASSERT_ALWAYS(op2 != NULL);
              llvm_values_per_lane[k][result_id] =
                  llvm_builder->CreateCall(spv_cross, {op1, op2});
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450UMin:
          case spv::GLSLstd450::GLSLstd450FMin: {
            ASSERT_ALWAYS(WordCount == 7);
            kto(opt_subgroup_size) {
              llvm::Value *op1 = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(op1 != NULL);
              llvm::Value *op2 = llvm_values_per_lane[k][pCode[6]];
              ASSERT_ALWAYS(op2 != NULL);
              llvm::Value *cmp = NULL;
              if (inst == spv::GLSLstd450::GLSLstd450UMin)
                cmp = llvm_builder->CreateICmpULT(op1, op2);
              else
                cmp = llvm_builder->CreateFCmpOLT(op1, op2);
              llvm::Value *select = llvm_builder->CreateSelect(cmp, op1, op2);
              llvm_values_per_lane[k][result_id] = select;
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450UMax:
          case spv::GLSLstd450::GLSLstd450FMax: {
            ASSERT_ALWAYS(WordCount == 7);
            kto(opt_subgroup_size) {
              llvm::Value *op1 = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(op1 != NULL);
              llvm::Value *op2 = llvm_values_per_lane[k][pCode[6]];
              ASSERT_ALWAYS(op2 != NULL);
              llvm::Value *cmp = NULL;
              if (inst == spv::GLSLstd450::GLSLstd450UMax)
                cmp = llvm_builder->CreateICmpUGT(op1, op2);
              else
                cmp = llvm_builder->CreateFCmpOGT(op1, op2);
              llvm::Value *select = llvm_builder->CreateSelect(cmp, op1, op2);
              llvm_values_per_lane[k][result_id] = select;
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450FSign: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(arg != NULL);
              if (arg->getType()->isVectorTy()) {
                llvm::VectorType *vtype =
                    llvm::dyn_cast<llvm::VectorType>(arg->getType());
                llvm::Value *cmp = llvm_builder->CreateFCmpOLT(
                    arg,
                    llvm_builder->CreateVectorSplat(
                        vtype->getVectorNumElements(),
                        llvm::ConstantFP::get(llvm::Type::getFloatTy(c), 0.0)));
                llvm::Value *select = llvm_builder->CreateSelect(
                    cmp,
                    llvm_builder->CreateVectorSplat(
                        vtype->getVectorNumElements(),
                        llvm::ConstantFP::get(llvm::Type::getFloatTy(c), -1.0)),
                    llvm_builder->CreateVectorSplat(
                        vtype->getVectorNumElements(),
                        llvm::ConstantFP::get(llvm::Type::getFloatTy(c), 1.0))

                );
                llvm_values_per_lane[k][result_id] = select;
              } else {
                llvm::Value *cmp = llvm_builder->CreateFCmpOLT(
                    arg, llvm::ConstantFP::get(llvm::Type::getFloatTy(c), 0.0));
                llvm::Value *select = llvm_builder->CreateSelect(
                    cmp, llvm::ConstantFP::get(llvm::Type::getFloatTy(c), -1.0),
                    llvm::ConstantFP::get(llvm::Type::getFloatTy(c), -1.0)

                );
                llvm_values_per_lane[k][result_id] = select;
              }
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450Reflect: {
            // I - 2 * dot(N, I) * N
            ASSERT_ALWAYS(WordCount == 7);
            kto(opt_subgroup_size) {
              llvm::Value *I = llvm_values_per_lane[k][word5];
              llvm::Value *N = llvm_values_per_lane[k][word6];
              NOTNULL(I);
              NOTNULL(N);
              llvm_values_per_lane[k][result_id] =
                  llvm_builder->CreateCall(spv_reflect, {I, N});
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450Pow: {
            // x^y
            ASSERT_ALWAYS(WordCount == 7);
            kto(opt_subgroup_size) {
              llvm::Value *x = llvm_values_per_lane[k][word5];
              llvm::Value *y = llvm_values_per_lane[k][word6];
              NOTNULL(x);
              NOTNULL(y);
              llvm_values_per_lane[k][result_id] =
                  llvm_builder->CreateCall(spv_pow, {x, y});
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450FClamp: {
          ASSERT_ALWAYS(WordCount == 8);
            kto(opt_subgroup_size) {
              llvm::Value *x = llvm_values_per_lane[k][word5];
              llvm::Value *min = llvm_values_per_lane[k][word6];
              llvm::Value *max = llvm_values_per_lane[k][word7];
              NOTNULL(x);
              NOTNULL(min);
              NOTNULL(max);
              llvm_values_per_lane[k][result_id] =
                  llvm_builder->CreateCall(spv_clamp_f32, {x, min, max});
            }
           break;
          }

          default:
            UNIMPLEMENTED_(get_cstr(inst));
          }
#undef ARG
          break;
        }
        case spv::Op::OpReturnValue: {
          ASSERT_ALWAYS(!contains(entries, item.first));
          uint32_t ret_value_id = word1;
          llvm::Value *arr = llvm::UndefValue::get(cur_fun->getReturnType());
          kto(opt_subgroup_size) {
            llvm::Value *ret_value = llvm_values_per_lane[k][ret_value_id];
            NOTNULL(ret_value);
            arr = llvm_builder->CreateInsertValue(arr, ret_value, (uint32_t)k);
          }
          llvm::ReturnInst::Create(c, arr, cur_bb);
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          cur_merge_id = -1;
          cur_continue_id = -1;
          break;
        }
        case spv::Op::OpReturn: {
          NOTNULL(cur_bb);
          if (contains(entries, item.first)) {
            for (auto &ds : deferred_stores) {
              llvm::Value *cast = llvm_builder->CreateBitCast(
                  ds.dst_ptr, ds.value_ptr->getType());
              llvm::Value *deref = llvm_builder->CreateLoad(ds.value_ptr);
              llvm_builder->CreateStore(deref, cast);
            }
          }
          llvm::ReturnInst::Create(c, NULL, cur_bb);
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          cur_merge_id = -1;
          cur_continue_id = -1;
          break;
        }
        case spv::Op::OpFNegate: {
          ASSERT_ALWAYS(WordCount == 4);
          uint32_t result_type_id = word1;
          uint32_t result_id = word2;
          uint32_t op_id = word3;
          kto(opt_subgroup_size) {
            llvm::Value *op = llvm_values_per_lane[k][op_id];
            NOTNULL(op);
            llvm_values_per_lane[k][result_id] = llvm_builder->CreateFNeg(op);
          }
          break;
        }
        case spv::Op::OpBitcast: {
          ASSERT_ALWAYS(WordCount == 4);
          kto(opt_subgroup_size) {
            uint32_t res_type_id = word1;
            uint32_t res_id = word2;
            uint32_t src_id = word3;
            llvm::Type *res_type = llvm_types[res_type_id];
            ASSERT_ALWAYS(res_type != NULL);
            llvm::Value *src = llvm_values_per_lane[k][src_id];
            ASSERT_ALWAYS(src != NULL);
            llvm_values_per_lane[k][res_id] =
                llvm_builder->CreateBitCast(src, res_type);
          }
          break;
        }
        case spv::Op::OpImageWrite: {
          // TODO: handle >4
          ASSERT_ALWAYS(WordCount == 4);
          uint32_t image_id = word1;
          uint32_t coord_id = word2;
          uint32_t texel_id = word3;
          kto(opt_subgroup_size) {
            llvm::Value *image = llvm_values_per_lane[k][image_id];
            ASSERT_ALWAYS(image != NULL);
            llvm::Value *coord = llvm_values_per_lane[k][coord_id];
            ASSERT_ALWAYS(coord != NULL);
            llvm::Value *texel = llvm_values_per_lane[k][texel_id];
            ASSERT_ALWAYS(texel != NULL);
            llvm_builder->CreateCall(lookup_image_op(texel->getType(),
                                                     coord->getType(),
                                                     /*read =*/false),
                                     {image, coord, texel});
          }
          break;
        }
        case spv::Op::OpImageRead: {
          // TODO: handle >5
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t res_type_id = word1;
          uint32_t res_id = word2;
          uint32_t image_id = word3;
          uint32_t coord_id = word4;
          kto(opt_subgroup_size) {
            llvm::Type *res_type = llvm_types[res_type_id];
            ASSERT_ALWAYS(res_type != NULL);
            llvm::Value *image = llvm_values_per_lane[k][image_id];
            ASSERT_ALWAYS(image != NULL);
            llvm::Value *coord = llvm_values_per_lane[k][coord_id];
            ASSERT_ALWAYS(coord != NULL);
            llvm::Value *call = llvm_builder->CreateCall(
                lookup_image_op(res_type, coord->getType(), /*read =*/true),
                {image, coord});
            llvm_values_per_lane[k][res_id] = call;
          }
          break;
        }
        case spv::Op::OpFunctionCall: {
          uint32_t fun_id = word3;
          uint32_t res_id = word2;
          llvm::Value *fun_value = llvm_global_values[fun_id];
          NOTNULL(fun_value);
          llvm::Function *target_fun =
              llvm::dyn_cast<llvm::Function>(fun_value);
          NOTNULL(target_fun);
          llvm::SmallVector<llvm::Value *, 4> args;
          // Prepend state * and mask
          args.push_back(state_ptr);
          args.push_back(cur_mask);

          for (int i = 4; i < WordCount; i++) {
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[i]];
              NOTNULL(arg);
              args.push_back(arg);
            }
          }
          llvm::Value *result = llvm_builder->CreateCall(target_fun, args);
          kto(opt_subgroup_size) {
            llvm_values_per_lane[k][res_id] =
                llvm_builder->CreateExtractValue(result, k);
          }
          break;
        }
        case spv::Op::OpMatrixTimesScalar: {
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t result_type_id = word1;
          uint32_t result_id = word2;
          uint32_t matrix_id = word3;
          uint32_t scalar_id = word4;
          kto(opt_subgroup_size) {
            llvm::Value *matrix = llvm_values_per_lane[k][matrix_id];
            llvm::Value *scalar = llvm_values_per_lane[k][scalar_id];
            NOTNULL(matrix);
            ASSERT_ALWAYS(
                matrix->getType()->isArrayTy() &&
                matrix->getType()->getArrayElementType()->isVectorTy());
            llvm::Value *result = llvm::UndefValue::get(matrix->getType());
            uint32_t row_size = matrix->getType()
                                    ->getArrayElementType()
                                    ->getVectorNumElements();
            ito(matrix->getType()->getArrayNumElements()) {
              llvm::Value *row = llvm_builder->CreateExtractValue(matrix, i);
              result = llvm_builder->CreateInsertValue(
                  result,
                  llvm_builder->CreateFMul(
                      row, llvm_builder->CreateVectorSplat(row_size, scalar)),
                  {0});
            }
            llvm_values_per_lane[k][result_id] = result;
          }
          break;
        }
        case spv::Op::OpVectorTimesMatrix: {
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t result_type_id = word1;
          uint32_t result_id = word2;
          uint32_t vector_id = word3;
          uint32_t matrix_id = word4;
          kto(opt_subgroup_size) {
            llvm::Value *matrix = llvm_values_per_lane[k][matrix_id];
            llvm::Value *vector = llvm_values_per_lane[k][vector_id];
            NOTNULL(matrix);
            ASSERT_ALWAYS(
                matrix->getType()->isArrayTy() &&
                matrix->getType()->getArrayElementType()->isVectorTy());

            uint32_t vector_size = vector->getType()->getVectorNumElements();
            ASSERT_ALWAYS(vector_size ==
                          matrix->getType()->getArrayNumElements());
            uint32_t matrix_row_size = matrix->getType()
                                           ->getArrayElementType()
                                           ->getVectorNumElements();
            llvm::Type *result_type = llvm::VectorType::get(
                vector->getType()->getVectorElementType(), matrix_row_size);
            llvm::Value *result = llvm::UndefValue::get(result_type);
            llvm::Value *matrix_t =
                llvm_matrix_transpose(matrix, llvm_builder.get());
            llvm::SmallVector<llvm::Value *, 4> rows;
            jto(matrix_row_size) {
              rows.push_back(llvm_builder->CreateExtractValue(matrix_t, j));
            }
            ito(matrix_row_size) {
              llvm::Value *dot_result =
                  llvm_dot(vector, rows[i], llvm_builder.get());
              result = llvm_builder->CreateInsertElement(result, dot_result, i);
            }
            llvm_values_per_lane[k][result_id] = result;
          }
          break;
        }
        case spv::Op::OpMatrixTimesVector: {
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t result_type_id = word1;
          uint32_t result_id = word2;
          uint32_t matrix_id = word3;
          uint32_t vector_id = word4;
          kto(opt_subgroup_size) {
            llvm::Value *matrix = llvm_values_per_lane[k][matrix_id];
            llvm::Value *vector = llvm_values_per_lane[k][vector_id];
            NOTNULL(matrix);
            ASSERT_ALWAYS(
                matrix->getType()->isArrayTy() &&
                matrix->getType()->getArrayElementType()->isVectorTy());

            uint32_t vector_size = vector->getType()->getVectorNumElements();

            uint32_t matrix_col_size = matrix->getType()->getArrayNumElements();
            ASSERT_ALWAYS(vector_size == matrix->getType()
                                             ->getArrayElementType()
                                             ->getVectorNumElements());

            llvm::Type *result_type = llvm::VectorType::get(
                vector->getType()->getVectorElementType(), matrix_col_size);
            llvm::Value *result = llvm::UndefValue::get(result_type);
            llvm::SmallVector<llvm::Value *, 4> rows;
            jto(matrix_col_size) {
              rows.push_back(llvm_builder->CreateExtractValue(matrix, j));
            }
            ito(matrix_col_size) {
              llvm::Value *dot_result =
                  llvm_dot(vector, rows[i], llvm_builder.get());
              result = llvm_builder->CreateInsertElement(result, dot_result, i);
            }
            // debug dumps
            //            {
            //              {
            //                llvm::Value *reinterpret =
            //                llvm_builder->CreateBitCast(
            //                    lookup_string("matrix times vector"),
            //                    llvm::Type::getInt8PtrTy(c));
            //                llvm_builder->CreateCall(dump_string, {state_ptr,
            //                reinterpret});
            //              }
            //              {
            //                llvm::Value *alloca =
            //                    llvm_builder->CreateAlloca(matrix->getType());
            //                llvm::Value *reinterpret =
            //                llvm_builder->CreateBitCast(
            //                    alloca, llvm::Type::getFloatPtrTy(c));
            //                llvm_builder->CreateStore(matrix, alloca);
            //                llvm_builder->CreateCall(dump_float4x4,
            //                                         {state_ptr,
            //                                         reinterpret});
            //              }
            //              {
            //                llvm::Value *reinterpret =
            //                llvm_builder->CreateBitCast(
            //                    lookup_string("X"),
            //                    llvm::Type::getInt8PtrTy(c));
            //                llvm_builder->CreateCall(dump_string, {state_ptr,
            //                reinterpret});
            //              }
            //              {
            //                llvm::Value *alloca =
            //                    llvm_builder->CreateAlloca(vector->getType());
            //                llvm::Value *reinterpret =
            //                llvm_builder->CreateBitCast(
            //                    alloca, llvm::Type::getFloatPtrTy(c));
            //                llvm_builder->CreateStore(vector, alloca);
            //                llvm_builder->CreateCall(dump_float4, {state_ptr,
            //                reinterpret});
            //              }
            //              {
            //                llvm::Value *reinterpret =
            //                llvm_builder->CreateBitCast(
            //                    lookup_string("="),
            //                    llvm::Type::getInt8PtrTy(c));
            //                llvm_builder->CreateCall(dump_string, {state_ptr,
            //                reinterpret});
            //              }
            //              {
            //                llvm::Value *alloca =
            //                    llvm_builder->CreateAlloca(vector->getType());
            //                llvm::Value *reinterpret =
            //                llvm_builder->CreateBitCast(
            //                    alloca, llvm::Type::getFloatPtrTy(c));
            //                llvm_builder->CreateStore(result, alloca);
            //                llvm_builder->CreateCall(dump_float4, {state_ptr,
            //                reinterpret});
            //              }
            //            }
            llvm_values_per_lane[k][result_id] = result;
          }
          break;
        }
        case spv::Op::OpMatrixTimesMatrix: {
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t result_type_id = word1;
          uint32_t result_id = word2;
          uint32_t matrix_1_id = word3;
          uint32_t matrix_2_id = word4;
          uto(opt_subgroup_size) {
            // MUl N x K x M matrices
            llvm::Value *matrix_1 = llvm_values_per_lane[u][matrix_1_id];
            llvm::Value *matrix_2 = llvm_values_per_lane[u][matrix_2_id];
            NOTNULL(matrix_1);
            NOTNULL(matrix_2);
            ASSERT_ALWAYS(
                matrix_1->getType()->isArrayTy() &&
                matrix_1->getType()->getArrayElementType()->isVectorTy());
            ASSERT_ALWAYS(
                matrix_2->getType()->isArrayTy() &&
                matrix_2->getType()->getArrayElementType()->isVectorTy());
            llvm::Type *elem_type = matrix_1->getType()
                                        ->getArrayElementType()
                                        ->getVectorElementType();
            uint32_t N = (uint32_t)matrix_1->getType()->getArrayNumElements();
            uint32_t K = matrix_1->getType()
                             ->getArrayElementType()
                             ->getVectorNumElements();
            uint32_t K_1 = (uint32_t)matrix_2->getType()->getArrayNumElements();
            ASSERT_ALWAYS(K == K_1);
            uint32_t M = matrix_2->getType()
                             ->getArrayElementType()
                             ->getVectorNumElements();

            llvm::Value *matrix_2_t =
                llvm_matrix_transpose(matrix_2, llvm_builder.get());
            llvm::SmallVector<llvm::Value *, 4> rows_1;
            llvm::SmallVector<llvm::Value *, 4> cols_2;
            jto(K) {
              cols_2.push_back(llvm_builder->CreateExtractValue(matrix_2_t, j));
            }
            jto(K) {
              rows_1.push_back(llvm_builder->CreateExtractValue(matrix_1, j));
            }
            llvm::Type *result_type =
                llvm::ArrayType::get(llvm::VectorType::get(elem_type, M), N);
            llvm::Value *result = llvm::UndefValue::get(result_type);
            ito(N) {
              llvm::Value *result_row =
                  llvm::UndefValue::get(result_type->getArrayElementType());
              jto(M) {
                llvm::Value *dot_result =
                    llvm_dot(rows_1[i], cols_2[j], llvm_builder.get());
                result_row = llvm_builder->CreateInsertElement(result_row,
                                                               dot_result, j);
              }
              result = llvm_builder->CreateInsertValue(result, result_row, i);
            }
            llvm_values_per_lane[u][result_id] = result;
          }
          break;
        }
        case spv::Op::OpImageSampleImplicitLod: {
          ASSERT_ALWAYS(WordCount == 5);
          uint32_t result_type_id = word1;
          uint32_t result_id = word2;
          uint32_t sampled_image_id = word3;
          uint32_t coordinate_id = word4;
          uint32_t dim = 0;
          llvm::Type *coord_elem_type = NULL;
          ito(opt_subgroup_size) {
            llvm::Value *coord = llvm_values_per_lane[i][coordinate_id];
            NOTNULL(coord);
            if (dim == 0) {
              if (coord->getType()->isVectorTy()) {
                dim = coord->getType()->getVectorNumElements();
                coord_elem_type = coord->getType()->getVectorElementType();
              } else {
                dim = 1;
                coord_elem_type = coord->getType();
              }
            } else {
              ASSERT_ALWAYS(dim == 1 ||
                            coord->getType()->isVectorTy() &&
                                coord->getType()->getVectorNumElements() ==
                                    dim);
              ASSERT_ALWAYS(dim == 1 && coord->getType() == coord_elem_type ||
                            coord->getType()->getVectorElementType() ==
                                coord_elem_type);
            }
          }
          llvm::SmallVector<llvm::Value *, 3> coordinates;
          //          llvm::Type *coordinate_array_type = llvm::ArrayType::get(
          //              llvm::ArrayType::get(coord_elem_type,
          //              opt_subgroup_size), dim);
          //          llvm::Type *deriv_array_type = llvm::ArrayType::get(
          //              llvm::ArrayType::get(llvm::VectorType::get(coord_elem_type,
          //              2),
          //                                   opt_subgroup_size),
          //              dim);
          //          llvm::Value *coordinate_array =
          //              llvm::UndefValue::get(coordinate_array_type);
          ito(dim) {
            llvm::Type *dim_array_type =
                llvm::ArrayType::get(coord_elem_type, opt_subgroup_size);
            llvm::Value *dim_array = llvm_builder->CreateAlloca(dim_array_type);
            llvm::Value *dim_ptr = llvm_builder->CreateBitCast(
                dim_array, llvm::PointerType::get(coord_elem_type, 0));
            jto(opt_subgroup_size) {
              llvm::Value *coord = llvm_values_per_lane[j][coordinate_id];
              llvm::Value *coordinate_elem =
                  llvm_builder->CreateExtractElement(coord, i);
              llvm::Value *gep =
                  llvm_builder->CreateGEP(dim_ptr, llvm_get_constant_i32(j));
              llvm_builder->CreateStore(coordinate_elem, gep);
            }
            coordinates.push_back(dim_array);
            //            llvm_builder->CreateInsertValue(
            //                coordinate_array,
            //                llvm_builder->CreateLoad(dim_array), i);
          }
          //          llvm::Value *coordinate_array_alloca =
          //              llvm_builder->CreateAlloca(coordinate_array_type);
          //          llvm_builder->CreateStore(coordinate_array,
          //          coordinate_array_alloca);
          llvm::SmallVector<llvm::Value *, 3> derivatives;
          ito(dim) {
            //            llvm::Value *row_ptr =
            //                llvm_builder->CreateGEP(coordinate_array_alloca,
            //                {0, i});
            llvm::Value *dim_deriv_alloca = llvm_builder->CreateAlloca(
                llvm::ArrayType::get(llvm::VectorType::get(coord_elem_type, 2),
                                     opt_subgroup_size));
            llvm::Value *float2_arr_to_ptr = llvm_builder->CreateBitCast(
                dim_deriv_alloca,
                llvm::PointerType::get(
                    llvm::VectorType::get(coord_elem_type, 2), 0));
            llvm_builder->CreateCall(
                get_derivatives, {state_ptr,
                                  llvm_builder->CreateBitCast(
                                      coordinates[i], llvm::PointerType::get(
                                                          coord_elem_type, 0)),
                                  float2_arr_to_ptr});
            //	    llvm::Value *dim_deriv = llvm_builder->CreateAlloca();
            //            derivatives.push_back();
            derivatives.push_back(dim_deriv_alloca);
          }
          if (dim == 1) {
            UNIMPLEMENTED;
          } else if (dim == 2) {
            llvm::Value *derivatives_x =
                llvm_builder->CreateLoad(derivatives[0]);
            llvm::Value *derivatives_y =
                llvm_builder->CreateLoad(derivatives[1]);
            ito(opt_subgroup_size) {
              llvm::Value *dudxdy =
                  llvm_builder->CreateExtractValue(derivatives_x, {i});
              llvm::Value *dvdxdy =
                  llvm_builder->CreateExtractValue(derivatives_y, {i});
              llvm::Value *dudx =
                  llvm_builder->CreateExtractElement(dudxdy, (uint64_t)0);
              llvm::Value *dvdx =
                  llvm_builder->CreateExtractElement(dvdxdy, (uint64_t)0);
              llvm::Value *dudy =
                  llvm_builder->CreateExtractElement(dudxdy, (uint64_t)1);
              llvm::Value *dvdy =
                  llvm_builder->CreateExtractElement(dvdxdy, (uint64_t)1);
              llvm::Value *combined_image =
                  llvm_values_per_lane[i][sampled_image_id];
              NOTNULL(combined_image);
              ASSERT_ALWAYS(combined_image->getType() == combined_image_t);
              llvm::Value *image_handle =
                  llvm_builder->CreateCall(get_combined_image, combined_image);
              llvm::Value *sampler_handle = llvm_builder->CreateCall(
                  get_combined_sampler, combined_image);
              llvm::Value *coord = llvm_values_per_lane[i][coordinate_id];
              llvm_values_per_lane[i][result_id] = llvm_builder->CreateCall(
                  spv_image_sample_2d_float4,
                  {image_handle, sampler_handle,
                   llvm_builder->CreateExtractElement(coord, (uint64_t)0),
                   llvm_builder->CreateExtractElement(coord, (uint64_t)1), dudx,
                   dudy, dvdx, dvdy});
            }
          } else {
            UNIMPLEMENTED;
          }
          break;
        }
        case spv::Op::OpSampledImage:
        case spv::Op::OpImageSampleExplicitLod:
        case spv::Op::OpImageSampleDrefImplicitLod:
        case spv::Op::OpImageSampleDrefExplicitLod:
        case spv::Op::OpImageSampleProjImplicitLod:
        case spv::Op::OpImageSampleProjExplicitLod:
        case spv::Op::OpImageSampleProjDrefImplicitLod:
        case spv::Op::OpImageSampleProjDrefExplicitLod: {
          module->dump();
          UNIMPLEMENTED;
        }
        // Skip declarations
        case spv::Op::OpFunctionParameter:
        case spv::Op::OpNop:
        case spv::Op::OpUndef:
        case spv::Op::OpSourceContinued:
        case spv::Op::OpSource:
        case spv::Op::OpSourceExtension:
        case spv::Op::OpName:
        case spv::Op::OpMemberName:
        case spv::Op::OpString:
        case spv::Op::OpLine:
        case spv::Op::OpExtension:
        case spv::Op::OpExtInstImport:
        case spv::Op::OpMemoryModel:
        case spv::Op::OpEntryPoint:
        case spv::Op::OpExecutionMode:
        case spv::Op::OpCapability:
        case spv::Op::OpTypeVoid:
        case spv::Op::OpTypeBool:
        case spv::Op::OpTypeInt:
        case spv::Op::OpTypeFloat:
        case spv::Op::OpTypeVector:
        case spv::Op::OpTypeMatrix:
        case spv::Op::OpTypeImage:
        case spv::Op::OpTypeSampler:
        case spv::Op::OpTypeSampledImage:
        case spv::Op::OpTypeArray:
        case spv::Op::OpTypeRuntimeArray:
        case spv::Op::OpTypeStruct:
        case spv::Op::OpTypeOpaque:
        case spv::Op::OpTypePointer:
        case spv::Op::OpTypeFunction:
        case spv::Op::OpTypeEvent:
        case spv::Op::OpTypeDeviceEvent:
        case spv::Op::OpTypeReserveId:
        case spv::Op::OpTypeQueue:
        case spv::Op::OpTypePipe:
        case spv::Op::OpTypeForwardPointer:
        case spv::Op::OpConstantTrue:
        case spv::Op::OpConstantFalse:
        case spv::Op::OpConstant:
        case spv::Op::OpConstantComposite:
        case spv::Op::OpConstantSampler:
        case spv::Op::OpConstantNull:
        case spv::Op::OpSpecConstantTrue:
        case spv::Op::OpSpecConstantFalse:
        case spv::Op::OpSpecConstant:
        case spv::Op::OpSpecConstantComposite:
        case spv::Op::OpSpecConstantOp:
        case spv::Op::OpFunction:
        case spv::Op::OpFunctionEnd:
        case spv::Op::OpDecorate:
        case spv::Op::OpMemberDecorate:
        case spv::Op::OpDecorationGroup:
        case spv::Op::OpGroupDecorate:
        case spv::Op::OpGroupMemberDecorate:
          break;
        // Not implemented
        case spv::Op::OpVariable:
        case spv::Op::OpImageTexelPointer:
        case spv::Op::OpCopyMemory:
        case spv::Op::OpCopyMemorySized:
        case spv::Op::OpInBoundsAccessChain:
        case spv::Op::OpPtrAccessChain:
        case spv::Op::OpArrayLength:
        case spv::Op::OpGenericPtrMemSemantics:
        case spv::Op::OpInBoundsPtrAccessChain:
        case spv::Op::OpVectorExtractDynamic:
        case spv::Op::OpVectorInsertDynamic:
        case spv::Op::OpCompositeInsert:
        case spv::Op::OpCopyObject:
        case spv::Op::OpTranspose:
        case spv::Op::OpImageFetch:
        case spv::Op::OpImageGather:
        case spv::Op::OpImageDrefGather:
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
        case spv::Op::OpSNegate:
        case spv::Op::OpOuterProduct:
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
        case spv::Op::OpAtomicSMin:
        case spv::Op::OpAtomicUMin:
        case spv::Op::OpAtomicSMax:
        case spv::Op::OpAtomicUMax:
        case spv::Op::OpAtomicAnd:
        case spv::Op::OpAtomicXor:
        case spv::Op::OpSwitch:
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
        case spv::Op::OpGroupNonUniformAll:
        case spv::Op::OpGroupNonUniformAny:
        case spv::Op::OpGroupNonUniformAllEqual:
        case spv::Op::OpGroupNonUniformBroadcast:
        case spv::Op::OpGroupNonUniformInverseBallot:
        case spv::Op::OpGroupNonUniformBallotBitExtract:
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
          //        case spv::Op::OpDecorateStringGOOGLE:
        case spv::Op::OpMemberDecorateString:
          //        case spv::Op::OpMemberDecorateStringGOOGLE:
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
        case spv::Op::
            OpSubgroupAvcImeEvaluateWithSingleReferenceStreaminoutINTEL:
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
        case spv::Op::
            OpSubgroupAvcImeGetWeightingPatternMinimumMotionVectorINTEL:
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
        default: {
          module->dump();
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        }
      }
      // @llvm/finish_function
      for (auto &cb : deferred_branches) {
        llvm::BasicBlock *bb = cb.bb;
        ASSERT_ALWAYS(bb != NULL);
        std::unique_ptr<llvm::IRBuilder<>> llvm_builder;
        llvm_builder.reset(new llvm::IRBuilder<>(bb, llvm::ConstantFolder()));
        llvm::BasicBlock *dst_true = llvm_labels[cb.true_id];
        llvm::BasicBlock *dst_false = llvm_labels[cb.false_id];
        NOTNULL(dst_true);
        NOTNULL(dst_false);
        llvm::Value *new_mask = cur_mask;
        kto(opt_subgroup_size) {
          llvm::Value *cond = llvm_values_per_lane[k][cb.cond_id];
          NOTNULL(cond);
          new_mask = llvm_builder->CreateInsertElement(
              new_mask,
              llvm_builder->CreateAnd(
                  llvm_builder->CreateExtractElement(new_mask, (uint64_t)0),
                  cond),
              (uint32_t)k);
        }
        // TODO(aschrein) actual mask handling
        llvm::Value *new_mask_packed = llvm_builder->CreateBitCast(
            new_mask, llvm::IntegerType::get(c, opt_subgroup_size));
        llvm::Value *new_mask_i64 = llvm_builder->CreateZExt(
            new_mask_packed, llvm::IntegerType::get(c, 64));
        llvm::Value *cmp =
            llvm_builder->CreateICmpEQ(new_mask_i64, llvm_get_constant_i64(0));
        llvm::BranchInst::Create(dst_true, dst_false, cmp, bb);
      }
      for (auto &j : deferred_jumps) {
        llvm::BasicBlock *bb = j.first;
        ASSERT_ALWAYS(bb != NULL);
        llvm::BasicBlock *dst = llvm_labels[j.second];
        NOTNULL(dst);
        llvm::BranchInst::Create(dst, bb);
      }
    finish_function:
      continue;
    }
    // @llvm/finish_module
    // Make a function that returns the size of private space required by this
    // module
    {
      llvm::Function *get_private_size = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_private_size",
          module.get());
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_private_size);
      llvm::ReturnInst::Create(
          c, llvm::ConstantInt::get(c, llvm::APInt(32, private_storage_size)),
          bb);
    }
    // Function returns the amount of items that this module generates
    // Like in this case there would be 4 items
    // OpName %17 "gl_PerVertex"
    // OpMemberName %17 0 "gl_Position"
    // OpMemberName %17 1 "gl_PointSize"
    // OpMemberName %17 2 "gl_ClipDistance"
    // OpMemberName %17 3 "gl_CullDistance"
    {
      llvm::Function *get_export_count = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_export_count",
          module.get());
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_export_count);
      uint32_t field_count = 0;
      for (auto &item : struct_types) {
        if (item.second.is_builtin) {
          ASSERT_ALWAYS(field_count == 0 &&
                        "Only one export structure per module is allowed");
          field_count = (uint32_t)item.second.member_types.size();
        }
      }
      llvm::ReturnInst::Create(
          c, llvm::ConstantInt::get(c, llvm::APInt(32, field_count)), bb);
    }
    // Now enumerate all the items that are exported
    {
      llvm::Function *get_export_items = llvm::Function::Create(
          llvm::FunctionType::get(llvm::Type::getVoidTy(c),
                                  {llvm::Type::getInt32PtrTy(c)}, false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_export_items",
          module.get());
      llvm::Value *ptr_arg = get_export_items->getArg(0);
      NOTNULL(ptr_arg);
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_export_items);
      std::unique_ptr<llvm::IRBuilder<>> llvm_builder;
      llvm_builder.reset(new llvm::IRBuilder<>(bb, llvm::ConstantFolder()));

      for (auto &item : struct_types) {
        uint32_t i = 0;
        if (item.second.is_builtin) {
          ASSERT_ALWAYS(i == 0);
          for (auto &member_builtin_id : item.second.member_builtins) {
            llvm::Value *gep =
                llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(i));
            llvm_builder->CreateStore(llvm_get_constant_i32(member_builtin_id),
                                      gep);
            i++;
          }
        }
      }
      llvm::ReturnInst::Create(c, bb);
    }
    {
      llvm::Function *get_input_count = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_input_count",
          module.get());
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_input_count);
      uint32_t input_count = 0;
      ito(input_sizes.size()) {
        if (input_sizes[i] != 0)
          input_count++;
      }
      llvm::ReturnInst::Create(
          c, llvm::ConstantInt::get(c, llvm::APInt(32, input_count)), bb);
    }
    {
      llvm::Function *get_input_stride = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_input_stride",
          module.get());
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_input_stride);
      llvm::ReturnInst::Create(
          c, llvm::ConstantInt::get(c, llvm::APInt(32, input_storage_size)),
          bb);
    }
    {
      llvm::Function *get_input_slots = llvm::Function::Create(
          llvm::FunctionType::get(llvm::Type::getVoidTy(c),
                                  {llvm::Type::getInt32PtrTy(c)}, false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_input_slots",
          module.get());
      llvm::Value *ptr_arg = get_input_slots->getArg(0);
      NOTNULL(ptr_arg);
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_input_slots);
      std::unique_ptr<llvm::IRBuilder<>> llvm_builder;
      llvm_builder.reset(new llvm::IRBuilder<>(bb, llvm::ConstantFolder()));
      uint32_t k = 0;
      ito(input_sizes.size()) {
        if (input_sizes[i] != 0) {
          llvm::Value *gep_0 =
              llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3));
          llvm::Value *gep_1 = llvm_builder->CreateGEP(
              ptr_arg, llvm_get_constant_i32(k * 3 + 1));
          llvm::Value *gep_2 = llvm_builder->CreateGEP(
              ptr_arg, llvm_get_constant_i32(k * 3 + 2));
          llvm_builder->CreateStore(llvm_get_constant_i32(i), gep_0);
          llvm_builder->CreateStore(llvm_get_constant_i32(input_offsets[i]),
                                    gep_1);
          llvm_builder->CreateStore(llvm_get_constant_i32(input_formats[i]),
                                    gep_2);
          k++;
        }
      }
      llvm::ReturnInst::Create(c, bb);
    }
    {
      llvm::Function *get_output_count = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_output_count",
          module.get());
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_output_count);
      uint32_t output_count = 0;
      ito(output_sizes.size()) {
        if (output_sizes[i] != 0)
          output_count++;
      }
      llvm::ReturnInst::Create(
          c, llvm::ConstantInt::get(c, llvm::APInt(32, output_count)), bb);
    }
    {
      llvm::Function *get_output_stride = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_output_stride",
          module.get());
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_output_stride);
      llvm::ReturnInst::Create(
          c, llvm::ConstantInt::get(c, llvm::APInt(32, output_storage_size)),
          bb);
    }
    {
      llvm::Function *get_output_slots = llvm::Function::Create(
          llvm::FunctionType::get(llvm::Type::getVoidTy(c),
                                  {llvm::Type::getInt32PtrTy(c)}, false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_output_slots",
          module.get());
      llvm::Value *ptr_arg = get_output_slots->getArg(0);
      NOTNULL(ptr_arg);
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_output_slots);
      std::unique_ptr<llvm::IRBuilder<>> llvm_builder;
      llvm_builder.reset(new llvm::IRBuilder<>(bb, llvm::ConstantFolder()));
      uint32_t k = 0;
      ito(output_sizes.size()) {
        if (output_sizes[i] != 0) {
          llvm::Value *gep_0 =
              llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3));
          llvm::Value *gep_1 = llvm_builder->CreateGEP(
              ptr_arg, llvm_get_constant_i32(k * 3 + 1));
          llvm::Value *gep_2 = llvm_builder->CreateGEP(
              ptr_arg, llvm_get_constant_i32(k * 3 + 2));
          llvm_builder->CreateStore(llvm_get_constant_i32(i), gep_0);
          llvm_builder->CreateStore(llvm_get_constant_i32(output_offsets[i]),
                                    gep_1);
          llvm_builder->CreateStore(llvm_get_constant_i32(output_formats[i]),
                                    gep_2);
          k++;
        }
      }
      llvm::ReturnInst::Create(c, bb);
    }
    {
      llvm::Function *get_subgroup_size = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_subgroup_size",
          module.get());
      llvm::BasicBlock *bb =
          llvm::BasicBlock::Create(c, "entry", get_subgroup_size);
      llvm::ReturnInst::Create(
          c, llvm::ConstantInt::get(c, llvm::APInt(32, opt_subgroup_size)), bb);
    }
    // TODO(aschrein): investigate why LLVM removes code after tail calls in
    // this module.
    // Disable tail call crap
    {
      for (auto &fun : module->functions()) {
        for (auto &bb : fun) {
          for (auto &inst : bb) {
            if (auto *call = llvm::dyn_cast<llvm::CallInst>(&inst)) {
              call->setTailCallKind(llvm::CallInst::TailCallKind::TCK_NoTail);
            }
          }
        }
      }
    }
    std::string str;
    llvm::raw_string_ostream os(str);
    if (verifyModule(*module, &os)) {
      fprintf(stderr, "%s", os.str().c_str());
      abort();
    } else {
      return llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
      //      fprintf(stdout, "Module verified!\n");
    }
  }

  void parse_meta(const uint32_t *pCode, size_t codeSize) {
    this->code = pCode;
    this->code_size = codeSize;
    ASSERT_ALWAYS(pCode[0] == spv::MagicNumber);
    ASSERT_ALWAYS(pCode[1] <= spv::Version);

    const uint32_t generator = pCode[2];
    const uint32_t ID_bound = pCode[3];

    ASSERT_ALWAYS(pCode[4] == 0);

    const uint32_t *opStart = pCode + 5;
    const uint32_t *opEnd = pCode + codeSize;
    pCode = opStart;
    uint32_t cur_function = 0;
#define CLASSIFY(id, TYPE) decl_types.push_back({id, TYPE});
    // First pass
    // Parse Meta data: types, decorations etc
    while (pCode < opEnd) {
      uint16_t WordCount = pCode[0] >> spv::WordCountShift;
      spv::Op opcode = spv::Op(pCode[0] & spv::OpCodeMask);
      uint32_t word1 = pCode[1];
      uint32_t word2 = WordCount > 2 ? pCode[2] : 0;
      uint32_t word3 = WordCount > 3 ? pCode[3] : 0;
      uint32_t word4 = WordCount > 4 ? pCode[4] : 0;
      uint32_t word5 = WordCount > 5 ? pCode[5] : 0;
      uint32_t word6 = WordCount > 6 ? pCode[6] : 0;
      uint32_t word7 = WordCount > 7 ? pCode[7] : 0;
      uint32_t word8 = WordCount > 8 ? pCode[8] : 0;
      uint32_t word9 = WordCount > 9 ? pCode[9] : 0;
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
        Entry e;
        e.id = word2;
        e.execution_model = (spv::ExecutionModel)word1;
        e.name = (char const *)(pCode + 3);
        entries[e.id] = e;
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
      case spv::Op::OpTypeRuntimeArray: {
        // Just as any ptr
        PtrTy type;
        type.id = word1;
        type.storage_class = spv::StorageClass::StorageClassMax;
        type.target_id = word2;
        ptr_types[word1] = type;
        CLASSIFY(type.id, DeclTy::RuntimeArrayTy);
        break;
      }
      case spv::Op::OpTypeStruct: {
        StructTy type;
        type.id = word1;
        type.is_builtin = false;
        for (uint16_t i = 2; i < WordCount; i++) {
          type.member_types.push_back(pCode[i]);
          type.member_offsets.push_back(0);
          type.member_builtins.push_back(spv::BuiltIn::BuiltInMax);
        }
        ito(type.member_types.size()) {
          if (has_member_decoration(spv::Decoration::DecorationBuiltIn, type.id,
                                    i)) {
            type.is_builtin = true;
            type.member_builtins[i] =
                (spv::BuiltIn)find_member_decoration(
                    spv::Decoration::DecorationBuiltIn, type.id, i)
                    .param1;
          } else {
            ASSERT_ALWAYS(type.is_builtin == false);
            type.member_offsets[i] =
                find_member_decoration(spv::Decoration::DecorationOffset,
                                       type.id, i)
                    .param1;
          }
        }
        type.size = 0;
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
        switch (var.storage) {
        // Uniform data visible to all invocations
        case spv::StorageClass::StorageClassUniform:
        // Same as uniform but read-only and may hava an initializer
        case spv::StorageClass::StorageClassUniformConstant:
        // Pipeline ouput
        case spv::StorageClass::StorageClassOutput:
        // Pipeline input
        case spv::StorageClass::StorageClassInput:
        // Global memory visible to the current invocation
        case spv::StorageClass::StorageClassPrivate:
        // Global memory visible to the current invocation
        case spv::StorageClass::StorageClassStorageBuffer:
        // Small chunck of data visible to anyone
        case spv::StorageClass::StorageClassPushConstant: {
          global_variables.push_back(var.id);
          break;
        }
        case spv::StorageClass::StorageClassFunction: {
          ASSERT_ALWAYS(cur_function != 0);
          local_variables[cur_function].push_back(word2);
          break;
        }
        default:
          UNIMPLEMENTED_(get_cstr(var.storage));
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
      case spv::Op::OpFunctionParameter: {
        ASSERT_ALWAYS(cur_function != 0);
        FunctionParameter fp;
        fp.id = word2;
        fp.type_id = word1;
        functions[cur_function].params.push_back(fp);
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
      case spv::Op::OpConstantTrue: {
        Constant c;
        c.id = word2;
        c.type = word1;
        c.i32_val = (uint32_t)~0;
        constants[word2] = c;
        CLASSIFY(c.id, DeclTy::Constant);
        break;
      }
      case spv::Op::OpConstantFalse: {
        Constant c;
        c.id = word2;
        c.type = word1;
        c.i32_val = 0;
        constants[word2] = c;
        CLASSIFY(c.id, DeclTy::Constant);
        break;
      }
      case spv::Op::OpConstantComposite: {
        ConstantComposite c;
        c.id = word2;
        c.type = word1;
        ASSERT_ALWAYS(WordCount > 3);
        ito(WordCount - 3) { c.components.push_back(pCode[i + 3]); }
        constants_composite[c.id] = c;
        CLASSIFY(c.id, DeclTy::ConstantComposite);
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
      case spv::Op::OpUndef:
      case spv::Op::OpSourceContinued:
      case spv::Op::OpString:
      case spv::Op::OpLine:
      case spv::Op::OpTypeOpaque:
      case spv::Op::OpTypeEvent:
      case spv::Op::OpTypeDeviceEvent:
      case spv::Op::OpTypeReserveId:
      case spv::Op::OpTypeQueue:
      case spv::Op::OpTypePipe:
      case spv::Op::OpTypeForwardPointer:
      case spv::Op::OpConstantSampler:
      case spv::Op::OpConstantNull:
      case spv::Op::OpSpecConstantTrue:
      case spv::Op::OpSpecConstantFalse:
      case spv::Op::OpSpecConstant:
      case spv::Op::OpSpecConstantComposite:
      case spv::Op::OpSpecConstantOp:
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
    // TODO handle more cases like WASM_32 etc
    auto get_pointer_size = []() { return sizeof(void *); };
    auto get_primitive_size = [](Primitive_t type) -> size_t {
      size_t size = 0;
      switch (type) {
      case Primitive_t::I1:
      case Primitive_t::I8:
      case Primitive_t::U8:
        size = 1;
        break;
      case Primitive_t::I16:
      case Primitive_t::U16:
        size = 2;
        break;
      case Primitive_t::I32:
      case Primitive_t::U32:
      case Primitive_t::F32:
        size = 4;
        break;
      case Primitive_t::I64:
      case Primitive_t::U64:
      case Primitive_t::F64:
        size = 8;
        break;
      default:
        UNIMPLEMENTED_(get_cstr(type));
      }
      ASSERT_ALWAYS(size != 0);
      return size;
    };
    std::function<size_t(uint32_t)> get_size =
        [&](uint32_t member_type_id) -> size_t {
      size_t size = 0;
      ASSERT_ALWAYS(decl_types_table.find(member_type_id) !=
                    decl_types_table.end());
      DeclTy decl_type = decl_types_table.find(member_type_id)->second;
      // Limit to primitives and vectors and arrays
      ASSERT_ALWAYS(
          decl_type == DeclTy::PrimitiveTy || decl_type == DeclTy::ArrayTy ||
          decl_type == DeclTy::MatrixTy ||
          decl_type == DeclTy::RuntimeArrayTy || decl_type == DeclTy::VectorTy);
      bool is_void = false;
      switch (decl_type) {
      case DeclTy::RuntimeArrayTy: {
        size = get_pointer_size();
        break;
      }
      case DeclTy::PrimitiveTy: {
        ASSERT_ALWAYS(contains(primitive_types, member_type_id));
        Primitive_t ptype = primitive_types[member_type_id].type;
        if (ptype == Primitive_t::Void) {
          is_void = true;
        } else
          size = get_primitive_size(ptype);
        break;
      }
      case DeclTy::VectorTy: {
        ASSERT_ALWAYS(contains(vector_types, member_type_id));
        VectorTy vtype = vector_types[member_type_id];
        uint32_t vmember_type_id = vtype.member_id;
        size = get_size(vmember_type_id) * vtype.width;
        break;
      }
      case DeclTy::ArrayTy: {
        ASSERT_ALWAYS(contains(array_types, member_type_id));
        ArrayTy atype = array_types[member_type_id];
        uint32_t amember_type_id = atype.member_id;
        uint32_t length = 0;
        ASSERT_ALWAYS(contains(constants, atype.width_id));
        length = constants[atype.width_id].i32_val;
        ASSERT_ALWAYS(length != 0);
        size = get_size(amember_type_id) * length;
        break;
      }
      case DeclTy::MatrixTy: {
        ASSERT_ALWAYS(contains(matrix_types, member_type_id));
        MatrixTy type = matrix_types[member_type_id];
        size = get_size(type.vector_id) * type.width;
        break;
      }
      default:
        UNIMPLEMENTED_(get_cstr(decl_type));
      }
      if (is_void)
        return 0;
      ASSERT_ALWAYS(size != 0);
      return size;
    };
    std::function<VkFormat(uint32_t)> get_format =
        [&](uint32_t member_type_id) -> VkFormat {
      ASSERT_ALWAYS(decl_types_table.find(member_type_id) !=
                    decl_types_table.end());
      DeclTy decl_type = decl_types_table.find(member_type_id)->second;
      // Limit to primitives and vectors and arrays
      ASSERT_ALWAYS(decl_type == DeclTy::PrimitiveTy ||
                    decl_type == DeclTy::VectorTy);
      switch (decl_type) {
      case DeclTy::PrimitiveTy: {
        ASSERT_ALWAYS(contains(primitive_types, member_type_id));
        Primitive_t ptype = primitive_types[member_type_id].type;
        switch (ptype) {
        case Primitive_t::F32:
          return VkFormat::VK_FORMAT_R32_SFLOAT;
        default:
          UNIMPLEMENTED;
        }
        UNIMPLEMENTED;
      }
      case DeclTy::VectorTy: {
        ASSERT_ALWAYS(contains(vector_types, member_type_id));
        VectorTy vtype = vector_types[member_type_id];
        uint32_t vmember_type_id = vtype.member_id;
        VkFormat member_format = get_format(vmember_type_id);
        switch (member_format) {
        case VkFormat::VK_FORMAT_R32_SFLOAT:
          switch (vtype.width) {
          case 2:
            return VkFormat::VK_FORMAT_R32G32_SFLOAT;
          case 3:
            return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
          case 4:
            return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
          default:
            UNIMPLEMENTED;
          }
        default:
          UNIMPLEMENTED;
        }
        UNIMPLEMENTED;
      }
      default:
        UNIMPLEMENTED_(get_cstr(decl_type));
      }
      UNIMPLEMENTED;
    };
    for (auto &item : decl_types) {
      decl_types_table[item.first] = item.second;
    }
    auto get_pointee_size = [&](uint32_t ptr_type_id) {
      ASSERT_ALWAYS(decl_types_table[ptr_type_id] == DeclTy::PtrTy);
      PtrTy ptr_type = ptr_types[ptr_type_id];
      return get_size(ptr_type.target_id);
    };
    auto get_pointee_format = [&](uint32_t ptr_type_id) {
      ASSERT_ALWAYS(decl_types_table[ptr_type_id] == DeclTy::PtrTy);
      PtrTy ptr_type = ptr_types[ptr_type_id];
      return get_format(ptr_type.target_id);
    };
    for (auto &item : decl_types) {
      uint32_t type_id = item.first;
      switch (item.second) {
      case DeclTy::FunTy:
        type_sizes[type_id] = 0;
        break;
      case DeclTy::PtrTy:
      case DeclTy::RuntimeArrayTy:
        type_sizes[type_id] = get_pointer_size();
        break;
      case DeclTy::StructTy: {
        ASSERT_HAS(struct_types);
        StructTy type = struct_types.find(type_id)->second;
        ASSERT_ALWAYS(type.member_offsets.size() > 0);
        uint32_t last_member_offset =
            type.member_offsets[type.member_offsets.size() - 1];
        type.size =
            last_member_offset + (uint32_t)get_size(type.member_types.back());
        type_sizes[type_id] = type.size;
        break;
      }
      case DeclTy::Function:
      case DeclTy::Constant:
      case DeclTy::Variable:
      case DeclTy::ConstantComposite:
        break;
      case DeclTy::ImageTy:
      case DeclTy::SamplerTy:
      case DeclTy::Sampled_ImageTy:
        type_sizes[type_id] = 4;
        break;
      default:
        type_sizes[type_id] = get_size(type_id);
      }
    }
    // Calculate the layout of the input and ouput data needed for optimal
    // work of the shader
    {
      std::vector<uint32_t> inputs;
      std::vector<uint32_t> outputs;
      uint32_t max_input_location = 0;
      uint32_t max_output_location = 0;
      // First pass figure out the number of input/output slots
      for (auto &item : decl_types) {
        if (item.second == DeclTy::Variable) {
          Variable var = variables[item.first];
          ASSERT_ALWAYS(var.id > 0);
          // Skip builtins here
          if (has_decoration(spv::Decoration::DecorationBuiltIn, var.id))
            continue;
          if (decl_types_table[var.type_id] == DeclTy::PtrTy) {
            PtrTy ptr_ty = ptr_types[var.type_id];
            if (decl_types_table[ptr_ty.target_id] == DeclTy::StructTy) {
              StructTy struct_ty = struct_types[ptr_ty.target_id];
              // This is a structure with builtin members so no need for
              // location
              if (struct_ty.is_builtin)
                continue;
            }
          }
          if (var.storage == spv::StorageClass::StorageClassInput) {
            uint32_t location =
                find_decoration(spv::Decoration::DecorationLocation, var.id)
                    .param1;
            if (inputs.size() <= location) {
              inputs.resize(location + 1);
            }
            inputs[location] = var.id;
            max_input_location = std::max(max_input_location, location);
          } else if (var.storage == spv::StorageClass::StorageClassOutput) {
            uint32_t location =
                find_decoration(spv::Decoration::DecorationLocation, var.id)
                    .param1;
            if (outputs.size() <= location) {
              outputs.resize(location + 1);
            }
            outputs[location] = var.id;
            max_output_location = std::max(max_output_location, location);
          }
        }
      }
      uint32_t input_offset = 0;
      uint32_t output_offset = 0;
      input_offsets.resize(max_input_location + 1);
      input_sizes.resize(max_input_location + 1);
      input_formats.resize(max_input_location + 1);
      output_offsets.resize(max_output_location + 1);
      output_sizes.resize(max_output_location + 1);
      output_formats.resize(max_output_location + 1);
      for (uint32_t id : inputs) {
        if (id > 0) {
          Variable var = variables[id];
          uint32_t location =
              find_decoration(spv::Decoration::DecorationLocation, var.id)
                  .param1;
          // Align to 16 bytes
          if ((input_offset & 0xf) != 0) {
            input_offset = (input_offset + 0xf) & (~0xfu);
          }
          input_offsets[location] = input_offset;
          uint32_t size = (uint32_t)get_pointee_size(var.type_id);
          input_sizes[location] = size;
          input_formats[location] = get_pointee_format(var.type_id);
          input_offset += size;
        }
      }
      for (uint32_t id : outputs) {
        if (id > 0) {
          Variable var = variables[id];
          uint32_t location =
              find_decoration(spv::Decoration::DecorationLocation, var.id)
                  .param1;
          // Align to 16 bytes
          if ((output_offset & 0xf) != 0) {
            output_offset = (output_offset + 0xf) & (~0xfu);
          }
          output_offsets[location] = output_offset;
          uint32_t size = (uint32_t)get_pointee_size(var.type_id);
          output_sizes[location] = size;
          output_formats[location] = get_pointee_format(var.type_id);
          output_offset += size;
        }
      }
      // Align to 16 bytes
      if ((input_offset & 0xf) != 0) {
        input_offset = (input_offset + 0xf) & (~0xfu);
      }
      // Align to 16 bytes
      if ((output_offset & 0xf) != 0) {
        output_offset = (output_offset + 0xf) & (~0xfu);
      }
      output_storage_size = output_offset;
      input_storage_size = input_offset;
    }
    for (auto &item : global_variables) {
      Variable var = variables[item];
      if (var.storage == spv::StorageClass::StorageClassPrivate) {
        // Usually it's a pointer but is it always?
        ASSERT_ALWAYS(contains(ptr_types, var.type_id));
        PtrTy ptr = ptr_types[var.type_id];
        uint32_t pointee_type_id = ptr.target_id;
        // Check that we don't have further indirections
        ASSERT_ALWAYS(
            //
            decl_types_table[pointee_type_id] == DeclTy::ArrayTy ||
            decl_types_table[pointee_type_id] == DeclTy::MatrixTy ||
            decl_types_table[pointee_type_id] == DeclTy::StructTy ||
            decl_types_table[pointee_type_id] == DeclTy::VectorTy ||
            decl_types_table[pointee_type_id] == DeclTy::PrimitiveTy
            //
        );
        private_offsets[var.id] = private_storage_size;
        ASSERT_ALWAYS(contains(type_sizes, pointee_type_id));
        private_storage_size += type_sizes[pointee_type_id];
      }
    }
    //    for (auto &item : struct_types) {
    //      for (auto &member_type_id : item.second.member_types) {
    //        if (decl_types_table[member_type_id] == DeclTy::MatrixTy) {
    //          MatrixTy mtype = matrix_types[member_type_id];
    //          ASSERT_ALWAYS(mtype.stride == 0 && "Matrix type is shared across
    //          structures?");
    //        }
    //      }
    //    }
  }
};

extern "C" {
void *compile_spirv(uint32_t const *pCode, size_t code_size) {
  Spirv_Builder builder;
  builder.parse_meta(pCode, code_size / 4);
  llvm::orc::ThreadSafeModule bundle = builder.build_llvm_module_vectorized();
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  llvm::ExitOnError ExitOnErr;

  // Create an LLJIT instance.
  std::unique_ptr<llvm::orc::LLJIT> J =
      ExitOnErr(llvm::orc::LLJITBuilder().create());
  ExitOnErr(J->addIRModule(std::move(bundle)));
  Jitted_Shader *jitted_shader = new Jitted_Shader();
  jitted_shader->jit = std::move(J);
  jitted_shader->init();
  return (void *)jitted_shader;
}

void release_spirv(void *ptr) {
  Jitted_Shader *jitted_shader = (Jitted_Shader *)ptr;
  delete jitted_shader;
}

Shader_Symbols *get_shader_symbols(void *ptr) {
  Jitted_Shader *jitted_shader = (Jitted_Shader *)ptr;
  return &jitted_shader->symbols;
}
}
#ifdef S2L_EXE
int main(int argc, char **argv) {
  ASSERT_ALWAYS(argc == 3);
  uint32_t subgroup_size = (uint32_t)atoi(argv[2]);
  ASSERT_ALWAYS(subgroup_size == 1 || subgroup_size == 4 ||
                subgroup_size == 64);
  size_t size;
  auto *bytes = read_file(argv[1], &size);
  defer(tl_free(bytes));

  const uint32_t *pCode = (uint32_t *)bytes;
  size_t codeSize = size / 4;
  Spirv_Builder builder;
  builder.opt_subgroup_size = subgroup_size;
  builder.parse_meta(pCode, codeSize);
  llvm::orc::ThreadSafeModule bundle = builder.build_llvm_module_vectorized();
  std::string str;
  llvm::raw_string_ostream os(str);
  str.clear();
  bundle.getModuleUnlocked()->print(os, NULL);
  os.flush();
  fprintf(stdout, "%s", str.c_str());
}
#endif // S2L_EXE
