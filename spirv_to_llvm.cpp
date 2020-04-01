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
#include "3rdparty/SPIRV/GLSL.std.450.h"
#include "3rdparty/SPIRV/spirv.hpp"
//#include <fstream>
//#include <iostream>
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

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llvm_stdlib.h"

template <typename T> struct std::vector<T> copy(std::vector<T> const &in) {
  return in;
}

template <typename K, typename V>
bool contains(std::map<K, V> const &in, K const &key) {
  return in.find(key) != in.end();
}

#define UNIMPLEMENTED_(s)                                                      \
  {                                                                            \
    fprintf(stderr, "UNIMPLEMENTED %s: %s:%i\n", s, __FILE__, __LINE__);       \
    (void)(*(int *)(void *)(0) = 0);                                           \
    exit(1);                                                                   \
  }

#define UNIMPLEMENTED UNIMPLEMENTED_("")

void WARNING(char const *fmt, ...) {
  static char buf[0x100];
  va_list argptr;
  va_start(argptr, fmt);
  vsnprintf(buf, sizeof(buf), fmt, argptr);
  va_end(argptr);
  fprintf(stderr, "[WARNING] %s\n", buf);
}

#define LOOKUP_FN(name)                                                        \
  llvm::Function *name = module->getFunction(#name);                           \
  ASSERT_ALWAYS(name != NULL);
#define LOOKUP_TY(name)                                                        \
  llvm::Type *name = module->getTypeByName(#name);                             \
  ASSERT_ALWAYS(name != NULL);

void llvm_fatal(void *user_data, const std::string &reason,
                bool gen_crash_diag) {
  fprintf(stderr, "[LLVM_FATAL] %s\n", reason.c_str());
  (void)(*(int *)(void *)(0) = 0);
}

#ifdef S2L_EXE

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

#include "spv_dump.hpp"

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
  // function_id -> [var_id...]
  std::map<uint32_t, std::vector<uint32_t>> local_variables;
  std::vector<uint32_t> global_variables;
  // function_id -> [inst*...]
  std::map<uint32_t, std::vector<uint32_t const *>> instructions;
  std::map<uint32_t, std::string> names;
  std::map<std::pair<uint32_t, uint32_t>, std::string> member_names;
  std::vector<const uint32_t *> entries;
  // Declaration order pairs
  std::vector<std::pair<uint32_t, DeclTy>> decl_types;
  std::map<uint32_t, DeclTy> decl_types_table;

  // Lifetime must be long enough
  uint32_t const *code;
  size_t code_size;

  auto find_decoration(spv::Decoration spv_dec, uint32_t val_id) -> Decoration {
    ASSERT_ALWAYS(contains(decorations, val_id));
    auto &decs = decorations[val_id];
    for (auto &dec : decs) {
      if (dec.type == spv_dec)
        return dec;
    }
    UNIMPLEMENTED;
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

  void build_llvm_module() {
    llvm::LLVMContext *context = new llvm::LLVMContext();
    auto &c = *context;
    llvm::SMDiagnostic error;
    auto mbuf = llvm::MemoryBuffer::getMemBuffer(
        llvm::StringRef((char *)llvm_stdlib_bc, llvm_stdlib_bc_len), "", false);
    llvm::Module *module = llvm::parseIR(*mbuf.get(), error, c).release();

    //    module->setDataLayout(NULL);
    ASSERT_ALWAYS(module);
    //    {
    //      llvm::Function *func = LOOKUP_DECL("sample_2d_f4");
    //      ASSERT_ALWAYS(func);
    //      func->dump();
    //    }

    llvm::install_fatal_error_handler(&llvm_fatal);

    const uint32_t *pCode = this->code;
    const uint32_t idbound = pCode[3];
    auto get_spv_name = [this](uint32_t id) -> std::string {
      if (names.find(id) == names.end()) {
        names[id] = "spv__" + std::to_string(id);
      }
      ASSERT_ALWAYS(names.find(id) != names.end());
      return names[id];
    };
    //    module->dump();

    // Initialize framework functions
    LOOKUP_FN(get_push_constant_ptr);
    LOOKUP_FN(get_uniform_ptr);
    LOOKUP_FN(get_uniform_const_ptr);
    LOOKUP_FN(get_input_ptr);
    LOOKUP_FN(get_output_ptr);
    LOOKUP_FN(kill);
    LOOKUP_FN(normalize_f2);
    LOOKUP_FN(normalize_f3);
    LOOKUP_FN(normalize_f4);
    LOOKUP_FN(length_f2);
    LOOKUP_FN(length_f3);
    LOOKUP_FN(length_f4);
    LOOKUP_FN(dummy_sample);
    LOOKUP_FN(spv_on_exit);
    LOOKUP_TY(sampler_t);
    LOOKUP_TY(image_t);
    LOOKUP_TY(combined_image_t);

    // Structure member offsets for GEP
    std::map<llvm::Type *, std::vector<uint32_t>> member_reloc;
    std::vector<llvm::Type *> llvm_types(idbound);
    std::vector<llvm::Value *> llvm_values(idbound);
    //    for (auto &item : vector_types) {
    //      dump(item.second);
    //    }
    // Map spirv types to llvm types
    char name_buf[0x100];
    for (auto &item : this->decl_types) {
      ASSERT_ALWAYS(llvm_types[item.first] == NULL &&
                    "Types must have unique ids");
      ASSERT_ALWAYS(llvm_values[item.first] == NULL &&
                    "Values must have unique ids");
      decl_types_table[item.first] = item.second;
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
        for (auto &param_id : type.params) {
          llvm::Type *arg_type = llvm_types[param_id];
          ASSERT_ALWAYS(arg_type != NULL &&
                        "Function must have all argumet types defined");
          args.push_back(arg_type);
        }
        llvm_types[type.id] = llvm::FunctionType::get(ret_type, args, false);
        break;
      }
      case DeclTy::Function: {
        ASSERT_HAS(functions);
        Function fun = functions.find(item.first)->second;
        llvm::FunctionType *fun_type =
            llvm::dyn_cast<llvm::FunctionType>(llvm_types[fun.function_type]);
        ASSERT_ALWAYS(fun_type != NULL && "Function type must be defined");
        llvm_values[fun.id] =
            llvm::Function::Create(fun_type, llvm::GlobalValue::ExternalLinkage,
                                   get_spv_name(fun.id), module);
        break;
      }
      case DeclTy::PtrTy: {
        ASSERT_HAS(ptr_types);
        PtrTy type = ptr_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.target_id];
        ASSERT_ALWAYS(elem_t != NULL && "Pointer target type must be defined");
        // Just map storage class to address space
        llvm_types[type.id] =
            llvm::PointerType::get(elem_t, (uint32_t)type.storage_class);
        break;
      }
      case DeclTy::ArrayTy: {
        ASSERT_HAS(array_types);
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
        if (!type->isFloatTy() &&
            !(type->isIntegerTy() && type->getIntegerBitWidth() == 32)) {
          UNIMPLEMENTED;
        }
        if (type->isFloatTy())
          llvm_values[c.id] =
              llvm::ConstantFP::get(type, llvm::APFloat(c.f32_val));
        else
          llvm_values[c.id] = llvm::ConstantInt::get(type, c.i32_val);
        break;
      }
      case DeclTy::Variable: {
        ASSERT_HAS(variables);
        Variable c = variables.find(item.first)->second;
        switch (c.storage) {
        case spv::StorageClass::StorageClassUniform:
        case spv::StorageClass::StorageClassUniformConstant:
        case spv::StorageClass::StorageClassOutput:
        case spv::StorageClass::StorageClassInput:
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
        std::vector<llvm::Type *> members;
        std::vector<size_t> offsets(type.member_types.size());
        ito(type.member_types.size()) offsets[i] =
            find_member_decoration(spv::Decoration::DecorationOffset, type.id,
                                   i)
                .param1;
        size_t offset = 0;
        // We manually insert padding bytes which offsets the structure members
        // for GEP instructions
        std::vector<uint32_t> member_indices;
        uint32_t index_offset = 0;
        for (uint32_t member_id = 0; member_id < type.member_types.size();
             member_id++) {
          llvm::Type *member_type = llvm_types[type.member_types[member_id]];
          ASSERT_ALWAYS(member_type != NULL && "Member types must be defined");
          if (offsets[member_id] != offset) {
            ASSERT_ALWAYS(offsets[member_id] > offset &&
                          "Can't move a member back in memory layout");
            size_t diff = offsets[member_id] - offset;
            // Push dummy bytes until the member offset is ok
            members.push_back(
                llvm::ArrayType::get(llvm::Type::getInt8Ty(c), diff));
            index_offset += 1;
          }
          size_t size = 0;
          uint32_t member_type_id = type.member_types[member_id];
          member_indices.push_back(index_offset);
          index_offset += 1;
          auto get_primitive_size = [](Primitive_t type) {
            size_t size = 0;
            switch (type) {
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
              UNIMPLEMENTED_(type);
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
            ASSERT_ALWAYS(decl_type == DeclTy::PrimitiveTy ||
                          decl_type == DeclTy::ArrayTy ||
                          decl_type == DeclTy::MatrixTy ||
                          decl_type == DeclTy::VectorTy);
            switch (decl_type) {
            case DeclTy::PrimitiveTy: {
              ASSERT_ALWAYS(contains(primitive_types, member_type_id));
              size = get_primitive_size(primitive_types[member_type_id].type);
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
            ASSERT_ALWAYS(size != 0);
            return size;
          };
          size = get_size(member_type_id);
          ASSERT_ALWAYS(size != 0);
          offset += size;
          members.push_back(member_type);
        }
        llvm::Type *struct_type =
            llvm::StructType::create(c, members, get_spv_name(type.id), true);
        member_reloc[struct_type] = member_indices;
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
      ASSERT_ALWAYS(
          (llvm_values[item.first] != NULL || llvm_types[item.first] != NULL) &&
          "eh there must be a type or value at the end!");
    }
    std::map<uint32_t, llvm::BasicBlock *> llvm_labels;
    // inst_id -> BB
    std::map<uint32_t, llvm::BasicBlock *> llvm_instbb;

    // Second pass:
    // Emit instructions
    auto &llvm_values_copy = llvm_values;
    for (auto &item : instructions) {
      uint32_t func_id = item.first;
      ASSERT_ALWAYS(llvm_values[func_id] != NULL);
      llvm::Function *cur_fun =
          llvm::dyn_cast<llvm::Function>(llvm_values[func_id]);
      ASSERT_ALWAYS(cur_fun != NULL);
      llvm::BasicBlock *cur_bb =
          llvm::BasicBlock::Create(c, "allocas", cur_fun, NULL);
      std::unique_ptr<llvm::IRBuilder<>> llvm_builder;
      llvm_builder.reset(new llvm::IRBuilder<>(cur_bb, llvm::ConstantFolder()));
      // @allocas
      // Make a local copy of llvm_values to make global->local shadows
      auto llvm_values = copy(llvm_values_copy);
      auto &locals = local_variables[func_id];
      for (auto &var_id : locals) {
        ASSERT_ALWAYS(variables.find(var_id) != variables.end());
        Variable var = variables.find(var_id)->second;
        ASSERT_ALWAYS(var.storage == spv::StorageClass::StorageClassFunction);
        llvm::Type *llvm_type = llvm_types[var.type_id];
        ASSERT_ALWAYS(llvm_type != NULL);
        llvm::Value *llvm_value = llvm_builder->CreateAlloca(
            llvm_type, 0, NULL, get_spv_name(var.id));
        if (var.init_id != 0) {
          llvm::Value *init_value = llvm_values[var.init_id];
          ASSERT_ALWAYS(init_value != NULL);
          llvm_builder->CreateStore(init_value, llvm_value);
        }
        llvm_values[var.id] = llvm_value;
      }

      // Make shadow variables for global state(push_constants, uniforms,
      // samplers etc)
      for (auto &var_id : global_variables) {
        ASSERT_ALWAYS(variables.find(var_id) != variables.end());
        Variable var = variables.find(var_id)->second;
        ASSERT_ALWAYS(var.storage != spv::StorageClass::StorageClassFunction);
        llvm::Type *llvm_type = llvm_types[var.type_id];
        ASSERT_ALWAYS(llvm_type != NULL);
        llvm::Value *llvm_value = llvm_builder->CreateAlloca(
            llvm_type, 0, NULL, get_spv_name(var.id));
        switch (var.storage) {
        case spv::StorageClass::StorageClassUniformConstant:
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
                  : get_uniform_ptr,
              {llvm_builder->getInt32((uint32_t)set),
               llvm_builder->getInt32((uint32_t)binding)});
          llvm::Value *cast = llvm_builder->CreateBitCast(pc_ptr, llvm_type);
          llvm_builder->CreateStore(cast, llvm_value);
          break;
        }
        case spv::StorageClass::StorageClassOutput: {
          uint32_t location =
              find_decoration(spv::Decoration::DecorationLocation, var.id)
                  .param1;

          ASSERT_ALWAYS(location >= 0);
          llvm::Value *pc_ptr = llvm_builder->CreateCall(
              get_output_ptr, {
                                  llvm_builder->getInt32((uint32_t)location),
                              });
          llvm::Value *cast = llvm_builder->CreateBitCast(pc_ptr, llvm_type);
          llvm_builder->CreateStore(cast, llvm_value);
          break;
        }
        case spv::StorageClass::StorageClassInput: {
          uint32_t location =
              find_decoration(spv::Decoration::DecorationLocation, var.id)
                  .param1;
          ASSERT_ALWAYS(location >= 0);
          llvm::Value *pc_ptr = llvm_builder->CreateCall(
              get_input_ptr, {
                                 llvm_builder->getInt32((uint32_t)location),
                             });
          llvm::Value *cast = llvm_builder->CreateBitCast(pc_ptr, llvm_type);
          llvm_builder->CreateStore(cast, llvm_value);
          break;
        }
        case spv::StorageClass::StorageClassPushConstant: {
          llvm::Value *pc_ptr = llvm_builder->CreateCall(get_push_constant_ptr);
          llvm::Value *cast = llvm_builder->CreateBitCast(pc_ptr, llvm_type);
          llvm_builder->CreateStore(cast, llvm_value);
          break;
        }
        case spv::StorageClass::StorageClassFunction: {
          break;
        }
        default:
          UNIMPLEMENTED_(get_cstr(var.storage));
        }

        if (var.init_id != 0) {
          ASSERT_ALWAYS(false);
        }
        llvm_values[var.id] = llvm_value;
      }
      std::vector<std::pair<llvm::BasicBlock *, uint32_t const *>>
          deferred_branches;
      for (uint32_t const *pCode : item.second) {
        uint16_t WordCount = pCode[0] >> spv::WordCountShift;
        spv::Op opcode = spv::Op(pCode[0] & spv::OpCodeMask);
        //        dump(opcode);dump("\n");
        uint32_t word1 = pCode[1];
        uint32_t word2 = WordCount > 2 ? pCode[2] : 0;
        uint32_t word3 = WordCount > 3 ? pCode[3] : 0;
        uint32_t word4 = WordCount > 4 ? pCode[4] : 0;
        uint32_t word5 = WordCount > 5 ? pCode[5] : 0;
        uint32_t word6 = WordCount > 6 ? pCode[6] : 0;
        uint32_t word7 = WordCount > 7 ? pCode[7] : 0;
        uint32_t word8 = WordCount > 8 ? pCode[8] : 0;
        uint32_t word9 = WordCount > 9 ? pCode[9] : 0;
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
          llvm::Value *base = llvm_values[word3];
          ASSERT_ALWAYS(base != NULL);
          llvm::Value *deref = llvm_builder->CreateLoad(base, "deref");
          llvm::PointerType *ptr_type =
              llvm::dyn_cast<llvm::PointerType>(deref->getType());
          ASSERT_ALWAYS(ptr_type != NULL);
          llvm::Type *pointee_type = ptr_type->getPointerElementType();
          ASSERT_ALWAYS(pointee_type != NULL);
          std::vector<llvm::Value *> indices = {
              llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), 0)};
          if (contains(member_reloc, pointee_type)) {
            std::vector<uint32_t> const &reloc_table =
                member_reloc[pointee_type];
            for (uint32_t i = 4; i < WordCount; i++) {
              llvm::Value *index_val = llvm_values[pCode[i]];
              ASSERT_ALWAYS(index_val != NULL);
              llvm::ConstantInt *integer =
                  llvm::dyn_cast<llvm::ConstantInt>(index_val);
              ASSERT_ALWAYS(
                  integer != NULL &&
                  "Access chain index must be OpConstant for structures");
              uint32_t cval = (uint32_t)integer->getLimitedValue();
              indices.push_back(llvm::ConstantInt::get(
                  llvm::Type::getInt32Ty(c), reloc_table[cval]));
            }
          } else {
            for (uint32_t i = 4; i < WordCount; i++) {
              llvm::Value *index_val = llvm_values[pCode[i]];
              ASSERT_ALWAYS(index_val != NULL);
              indices.push_back(index_val);
            }
          }

          llvm::Value *val = llvm_builder->CreateGEP(deref, indices);
          llvm::Value *alloca = llvm_builder->CreateAlloca(val->getType(), 0,
                                                           NULL, "stack_proxy");
          llvm_values[word2] = alloca;
          llvm_builder->CreateStore(val, alloca);
          break;
        }
        case spv::Op::OpLoad: {
          ASSERT_ALWAYS(cur_bb != NULL);
          llvm::Value *addr = llvm_values[word3];
          ASSERT_ALWAYS(addr != NULL);
          llvm::PointerType *ptr_type =
              llvm::dyn_cast<llvm::PointerType>(addr->getType());
          ASSERT_ALWAYS(ptr_type != NULL);
          llvm::Value *deref = llvm_builder->CreateLoad(addr, "deref");
          llvm_values[word2] = llvm_builder->CreateLoad(deref);
          break;
        }
        case spv::Op::OpStore: {
          ASSERT_ALWAYS(cur_bb != NULL);
          llvm::Value *addr = llvm_values[word1];
          ASSERT_ALWAYS(addr != NULL);
          llvm::Value *val = llvm_values[word2];
          ASSERT_ALWAYS(val != NULL);
          llvm::Value *deref = llvm_builder->CreateLoad(addr, "deref");
          llvm_builder->CreateStore(val, deref);
          break;
        }
        case spv::Op::OpBranch:
        case spv::Op::OpBranchConditional: {
          // branches reference labels that haven't been created yet
          // So we just fix this up later
          deferred_branches.push_back({cur_bb, pCode});
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          break;
        }
#define SIMPLE_LLVM_OP(llvm_op)                                                \
  ASSERT_ALWAYS(llvm_values[word2] == NULL);                                   \
  ASSERT_ALWAYS(llvm_values[word3] != NULL);                                   \
  ASSERT_ALWAYS(llvm_values[word4] != NULL);                                   \
  llvm_values[word2] =                                                         \
      llvm_builder->llvm_op(llvm_values[word3], llvm_values[word4]);
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
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpSRem: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpSMod: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFRem: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpFMod: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpLogicalEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpLogicalNotEqual: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpLogicalOr: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpLogicalAnd: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpLogicalNot: {
          UNIMPLEMENTED_(get_cstr(opcode));
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
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpShiftRightArithmetic: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpShiftLeftLogical: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpBitwiseOr: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpBitwiseXor: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpBitwiseAnd: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpNot: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        case spv::Op::OpKill: {
          llvm_builder->CreateCall(kill);
          break;
        }
        case spv::Op::OpVectorTimesScalar: {
          llvm::Value *vector = llvm_values[word3];
          llvm::Value *scalar = llvm_values[word4];
          ASSERT_ALWAYS(vector != NULL && scalar != NULL);
          llvm::VectorType *vtype =
              llvm::dyn_cast<llvm::VectorType>(vector->getType());
          ASSERT_ALWAYS(vtype != NULL);
          llvm::Value *splat = llvm_builder->CreateVectorSplat(
              vtype->getVectorNumElements(), scalar);
          llvm_values[word2] = llvm_builder->CreateFMul(vector, splat);
          break;
        }
        case spv::Op::OpCompositeExtract: {
          llvm::Value *src = llvm_values[word3];
          llvm::Type *src_type = src->getType();
          if (src_type->isArrayTy()) {
            UNIMPLEMENTED;
          } else if (src_type->isVectorTy()) {
            llvm_values[word2] = llvm_builder->CreateExtractElement(src, word4);
          } else {
            UNIMPLEMENTED;
          }
          break;
        }
        case spv::Op::OpCompositeConstruct: {
          llvm::Type *dst_type = llvm_types[word1];
          ASSERT_ALWAYS(dst_type != NULL);
          if (dst_type->isVectorTy()) {
            llvm::Value *undef = llvm::UndefValue::get(dst_type);
            llvm::VectorType *vtype =
                llvm::dyn_cast<llvm::VectorType>(dst_type);
            ASSERT_ALWAYS(vtype != NULL);
            llvm::Value *final_val = undef;
            ito(vtype->getVectorNumElements()) {
              llvm::Value *src = llvm_values[pCode[3 + i]];
              ASSERT_ALWAYS(src != NULL);
              final_val = llvm_builder->CreateInsertElement(final_val, src, i);
            }
            llvm_values[word2] = final_val;
          } else {
            UNIMPLEMENTED;
          }

          break;
        }
        case spv::Op::OpVectorShuffle: {
          llvm::Value *op1 = llvm_values[word3];
          llvm::Value *op2 = llvm_values[word4];
          ASSERT_ALWAYS(op1 != NULL && op2 != NULL);
          llvm::VectorType *vtype1 =
              llvm::dyn_cast<llvm::VectorType>(op1->getType());
          llvm::VectorType *vtype2 =
              llvm::dyn_cast<llvm::VectorType>(op2->getType());
          ASSERT_ALWAYS(vtype1 != NULL && vtype2 != NULL);
          std::vector<uint32_t> indices;
          for (uint16_t i = 5; i < WordCount; i++)
            indices.push_back(pCode[i]);
          if (vtype1->getVectorNumElements() !=
              vtype2->getVectorNumElements()) {
            UNIMPLEMENTED;
          } else {
            ASSERT_ALWAYS(llvm_builder && cur_bb != NULL);
            llvm_values[word2] =
                llvm_builder->CreateShuffleVector(op1, op2, indices);
          }
          break;
        }
        case spv::Op::OpExtInst: {
          spv::GLSLstd450 inst = (spv::GLSLstd450)pCode[4];
#define ARG(n) (llvm_values[pCode[5 + n]])
          switch (inst) {
          case spv::GLSLstd450::GLSLstd450Normalize: {
            ASSERT_ALWAYS(WordCount == 6);
            llvm::Value *arg = ARG(0);
            ASSERT_ALWAYS(arg != NULL);
            llvm::VectorType *vtype =
                llvm::dyn_cast<llvm::VectorType>(arg->getType());
            ASSERT_ALWAYS(vtype != NULL);
            uint32_t width = vtype->getVectorNumElements();
            switch (width) {
            case 2:
              llvm_values[word2] =
                  llvm_builder->CreateCall(normalize_f2, {ARG(0)});
              break;
            case 3:
              llvm_values[word2] =
                  llvm_builder->CreateCall(normalize_f3, {ARG(0)});
              break;
            case 4:
              llvm_values[word2] =
                  llvm_builder->CreateCall(normalize_f4, {ARG(0)});
              break;
            default:
              UNIMPLEMENTED;
            }

            break;
          }
          case spv::GLSLstd450::GLSLstd450Length: {
            ASSERT_ALWAYS(WordCount == 6);
            llvm::Value *arg = ARG(0);
            ASSERT_ALWAYS(arg != NULL);
            llvm::VectorType *vtype =
                llvm::dyn_cast<llvm::VectorType>(arg->getType());
            ASSERT_ALWAYS(vtype != NULL);
            uint32_t width = vtype->getVectorNumElements();
            switch (width) {
            case 2:
              llvm_values[word2] =
                  llvm_builder->CreateCall(length_f2, {ARG(0)});
              break;
            case 3:
              llvm_values[word2] =
                  llvm_builder->CreateCall(length_f3, {ARG(0)});
              break;
            case 4:
              llvm_values[word2] =
                  llvm_builder->CreateCall(length_f4, {ARG(0)});
              break;
            default:
              UNIMPLEMENTED;
            }

            break;
          }
          default:
            UNIMPLEMENTED_(get_cstr(inst));
          }
#undef ARG
          break;
        }
        case spv::Op::OpReturn: {
          // Skip
          break;
        }
        case spv::Op::OpSampledImage:
        case spv::Op::OpImageSampleImplicitLod:
        case spv::Op::OpImageSampleExplicitLod:
        case spv::Op::OpImageSampleDrefImplicitLod:
        case spv::Op::OpImageSampleDrefExplicitLod:
        case spv::Op::OpImageSampleProjImplicitLod:
        case spv::Op::OpImageSampleProjExplicitLod:
        case spv::Op::OpImageSampleProjDrefImplicitLod:
        case spv::Op::OpImageSampleProjDrefExplicitLod: {
          llvm::Value *val = llvm_builder->CreateCall(dummy_sample);
          //          llvm::Value *alloca =
          //          llvm_builder->CreateAlloca(val->getType());
          //          llvm_builder->CreateStore(val, alloca);
          llvm_values[word2] = val;
          WARNING("skipping %s", get_cstr(opcode));
          break;
        }
        // Skip structured control flow instructions for now
        case spv::Op::OpLoopMerge:
        case spv::Op::OpSelectionMerge:
          break;
        // Skip declarations
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
        case spv::Op::OpFunctionParameter:
        case spv::Op::OpFunctionEnd:
        case spv::Op::OpDecorate:
        case spv::Op::OpMemberDecorate:
        case spv::Op::OpDecorationGroup:
        case spv::Op::OpGroupDecorate:
        case spv::Op::OpGroupMemberDecorate:
          break;
        // Not implemented
        case spv::Op::OpFunctionCall:
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
        case spv::Op::OpSwitch:
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
        case spv::Op::OpMax: {
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        }
      }
    finish_function:

      for (auto &item : deferred_branches) {
        uint32_t const *pCode = item.second;
        llvm::BasicBlock *bb = item.first;
        ASSERT_ALWAYS(pCode != NULL && bb != NULL);
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
        switch (opcode) {
        case spv::Op::OpBranch: {
          llvm::BasicBlock *dst = llvm_labels[word1];
          ASSERT_ALWAYS(dst != NULL);
          llvm::BranchInst::Create(dst, bb);
          break;
        }
        case spv::Op::OpBranchConditional: {
          llvm::BasicBlock *dst_true = llvm_labels[word2];
          llvm::BasicBlock *dst_false = llvm_labels[word3];
          llvm::Value *cond = llvm_values[word1];
          ASSERT_ALWAYS(dst_true != NULL && dst_false != NULL && cond != NULL);
          llvm::BranchInst::Create(dst_true, dst_false, cond, bb);
          break;
        }
        default:
          UNIMPLEMENTED_(opcode);
        }
      }

      llvm::BasicBlock *exit_bb = llvm::BasicBlock::Create(c, "exit");
      llvm::CallInst::Create(spv_on_exit, "", exit_bb);
      llvm::ReturnInst::Create(c, NULL, exit_bb);
      exit_bb->insertInto(cur_fun);
      llvm::BranchInst::Create(exit_bb, cur_bb);
    }
    // @llvm/print

    std::string str;
    llvm::raw_string_ostream os(str);
    str.clear();
    module->print(os, NULL);
    os.flush();
    fprintf(stdout, "%s", str.c_str());
    if (verifyModule(*module, &os)) {
      fprintf(stderr, "%s", os.str().c_str());
      exit(1);
    } else {
//      fprintf(stdout, "Module verified!\n");
    }
  }
  void parse_meta(const uint32_t *pCode, size_t codeSize) {
    this->code = pCode;
    this->code_size = code_size;
    ASSERT_ALWAYS(pCode[0] == spv::MagicNumber);
    ASSERT_ALWAYS(pCode[1] <= spv::Version);

    const uint32_t generator = pCode[2];
    const uint32_t idbound = pCode[3];

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
        switch (var.storage) {
        case spv::StorageClass::StorageClassUniform:
        case spv::StorageClass::StorageClassUniformConstant:
        case spv::StorageClass::StorageClassOutput:
        case spv::StorageClass::StorageClassInput:
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
  size_t size;
  auto *bytes = read_file(argv[1], &size);
  defer(tl_free(bytes));

  const uint32_t *pCode = (uint32_t *)bytes;
  size_t codeSize = size / 4;
  Spirv_Builder builder;
  builder.parse_meta(pCode, codeSize);
  builder.build_llvm_module();
}
#endif // S2L_EXE
