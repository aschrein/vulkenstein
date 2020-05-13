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
#include "llvm/InitializePasses.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Vectorize.h"
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <spirv-tools/libspirv.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llvm_stdlib_1.h"
#include "llvm_stdlib_4.h"
#include "llvm_stdlib_64.h"

#define LOOKUP_FN(name)                                                                            \
  llvm::Function *name = module->getFunction(#name);                                               \
  ASSERT_ALWAYS(name != NULL);
#define LOOKUP_TY(name)                                                                            \
  llvm::Type *name = module->getTypeByName(#name);                                                 \
  ASSERT_ALWAYS(name != NULL);

void llvm_fatal(void *user_data, const std::string &reason, bool gen_crash_diag) {
  fprintf(stderr, "[LLVM_FATAL] %s\n", reason.c_str());
  abort();
}

static void WARNING(char const *fmt, ...) {
  static char buf[0x100];
  va_list     argptr;
  va_start(argptr, fmt);
  vsnprintf(buf, sizeof(buf), fmt, argptr);
  va_end(argptr);
  fprintf(stderr, "[WARNING] %s\n", buf);
}

void *read_file(const char *filename, size_t *size, Allocator *allocator = NULL) {
  if (allocator == NULL) allocator = Allocator::get_default();
  FILE *f = fopen(filename, "rb");
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  *size      = (size_t)fsize;
  char *data = (char *)allocator->alloc((size_t)fsize);
  fread(data, 1, (size_t)fsize, f);
  fclose(f);
  return data;
}

// Lives on the heap, so pointers are persistent
struct Jitted_Shader {

  class MyObjectCache : public llvm::ObjectCache {
public:
    void notifyObjectCompiled(const llvm::Module *M, llvm::MemoryBufferRef ObjBuffer) override {
      auto buf = llvm::MemoryBuffer::getMemBufferCopy(ObjBuffer.getBuffer(),
                                                      ObjBuffer.getBufferIdentifier());
      if (dump) {
        TMP_STORAGE_SCOPE;
        char tmp_buf[0x100];
        // Dump object file
        {
          string_ref dir = stref_s("shader_dumps/obj/");
          make_dir_recursive(dir);
          snprintf(tmp_buf, sizeof(tmp_buf), "%lx.o", code_hash);
          string_ref final_path = stref_concat(dir, stref_s(tmp_buf));
          dump_file(stref_to_tmp_cstr(final_path), buf->getBufferStart(),
                    (size_t)(buf->getBufferSize()));
        }
      }
    }

    std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module *M) override {
      return nullptr;
    }
    bool dump = false;
    u64  code_hash;
  };

  void init(u64 code_hash, bool debug_info = true, bool dump = false) {
    obj_cache.dump      = dump;
    obj_cache.code_hash = code_hash;
    llvm::ExitOnError ExitOnErr;

    static_cast<llvm::orc::RTDyldObjectLinkingLayer &>(jit->getObjLinkingLayer())
        .registerJITEventListener(*llvm::JITEventListener::createGDBRegistrationListener());
    jit->getIRTransformLayer().setTransform(
        [&](llvm::orc::ThreadSafeModule TSM, const llvm::orc::MaterializationResponsibility &R) {
          TSM.withModuleDo([&](llvm::Module &M) {

          });
          return TSM;
        });
    jit->getMainJITDylib().addGenerator(
        ExitOnErr(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            jit->getDataLayout().getGlobalPrefix())));
#define LOOKUP(name)                                                                               \
  symbols.name = (typeof(symbols.name))ExitOnErr(jit->lookup(#name)).getAddress();
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
    symbols.get_input_slots((u32 *)&symbols.input_slots[0]);
    symbols.output_item_count = symbols.get_output_count();
    symbols.input_stride      = symbols.get_input_stride();
    symbols.output_stride     = symbols.get_output_stride();
    symbols.subgroup_size     = symbols.get_subgroup_size();
    symbols.get_output_slots((u32 *)&symbols.output_slots[0]);
    symbols.private_storage_size = symbols.get_private_size();
    symbols.export_count         = symbols.get_export_count();
    symbols.get_export_items(&symbols.export_items[0]);
    symbols.code_hash = code_hash;

    //    std::string str;
    //    llvm::raw_string_ostream os(str);
    //    jit->getMainJITDylib().dump(os);
    //    os.flush();
    //    fprintf(stdout, "%s", str.c_str());
  }
  MyObjectCache                     obj_cache;
  Shader_Symbols                    symbols;
  std::unique_ptr<llvm::orc::LLJIT> jit;
};
namespace llvm {
struct My_Dump_Pass : llvm::ModulePass {
  static char ID;
  u64         code_hash;
  explicit My_Dump_Pass(u64 code_hash = 0) : llvm::ModulePass(ID), code_hash(code_hash) {}

  virtual bool runOnModule(llvm::Module &M) override {
    static u32 dump_id = 0;
    TMP_STORAGE_SCOPE;
    char buf[0x100];
    // Print llvm dumps
    {
      std::string              str;
      llvm::raw_string_ostream os(str);
      M.print(os, NULL);
      os.flush();
      string_ref dir = stref_s("shader_dumps/llvm/");
      make_dir_recursive(dir);
      snprintf(buf, sizeof(buf), "%lx.%i.ll", code_hash, dump_id++);
      string_ref final_path = stref_concat(dir, stref_s(buf));
      dump_file(stref_to_tmp_cstr(final_path), str.c_str(), str.length());
    }
    return false;
  }
};

char My_Dump_Pass::ID = 0;
} // namespace llvm
llvm::My_Dump_Pass *create_my_dump_pass(u64 code_hash) { return new llvm::My_Dump_Pass(code_hash); }

//////////////////////////
// Meta data structures //
//////////////////////////
struct FunTy {
  u32              id;
  std::vector<u32> params;
  u32              ret;
};
struct ImageTy {
  u32                  id;
  u32                  sampled_type;
  spv::Dim             dim;
  bool                 depth;
  bool                 arrayed;
  bool                 ms;
  u32                  sampled;
  spv::ImageFormat     format;
  spv::AccessQualifier access;
};
struct Sampled_ImageTy {
  u32 id;
  u32 sampled_image;
};
struct SamplerTy {
  u32 id;
};
struct Decoration {
  u32             target_id;
  spv::Decoration type;
  u32             param1;
  u32             param2;
  u32             param3;
  u32             param4;
};
struct Member_Decoration {
  u32             target_id;
  u32             member_id;
  spv::Decoration type;
  u32             param1;
  u32             param2;
  u32             param3;
  u32             param4;
};
enum class Primitive_t { I1, I8, I16, I32, I64, U8, U16, U32, U64, F8, F16, F32, F64, Void };
struct PrimitiveTy {
  u32         id;
  Primitive_t type;
};
struct VectorTy {
  u32 id;
  // Must be primitive?
  u32 member_id;
  // the number of rows
  u32 width;
};
struct Constant {
  u32 id;
  u32 type;
  union {
    u32   i32_val;
    float f32_val;
  };
};
struct ConstantComposite {
  u32              id;
  u32              type;
  std::vector<u32> components;
};
struct ArrayTy {
  u32 id;
  // could be anything?
  u32 member_id;
  // constants[width_id]
  u32 width_id;
};
struct MatrixTy {
  u32 id;
  // vector_types[vector_id] : column type
  u32 vector_id;
  // the number of columns
  u32 width;
};
struct StructTy {
  u32              id;
  bool             is_builtin;
  std::vector<u32> member_types;
  std::vector<u32> member_offsets;
  // Apparently there could be stuff like that
  // out gl_PerVertex
  // {
  //    vec4 gl_Position;
  // };
  std::vector<spv::BuiltIn> member_builtins;
  u32                       size;
};
struct PtrTy {
  u32               id;
  u32               target_id;
  spv::StorageClass storage_class;
};
struct Variable {
  u32 id;
  // Must be a pointer
  u32               type_id;
  spv::StorageClass storage;
  // Optional
  u32 init_id;
};
struct FunctionParameter {
  u32 id;
  u32 type_id;
};
struct Function {
  u32                            id;
  u32                            result_type;
  spv::FunctionControlMask       control;
  u32                            function_type;
  std::vector<FunctionParameter> params;
  // Debug mapping
  u32 spirv_line;
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
  u32                 id;
  spv::ExecutionModel execution_model;
  std::string         name;
};
struct Parsed_Op {
  spv::Op          op;
  std::vector<u32> args;
};
#include "spv_dump.hpp"

struct Spirv_Builder {
  //////////////////////
  //     Options      //
  //////////////////////
  u32  opt_subgroup_size  = 1;
  bool opt_debug_comments = false;
  bool opt_debug_info     = false;
  bool opt_dump           = true;
  bool opt_llvm_dump      = false;
  //  bool opt_deinterleave_attributes = false;

  //////////////////////
  // Meta information //
  //////////////////////
  std::map<u32, PrimitiveTy>                    primitive_types;
  std::map<u32, Variable>                       variables;
  std::map<u32, Function>                       functions;
  std::map<u32, PtrTy>                          ptr_types;
  std::map<u32, VectorTy>                       vector_types;
  std::map<u32, Constant>                       constants;
  std::map<u32, ConstantComposite>              constants_composite;
  std::map<u32, ArrayTy>                        array_types;
  std::map<u32, ImageTy>                        images;
  std::map<u32, SamplerTy>                      samplers;
  std::map<u32, Sampled_ImageTy>                combined_images;
  std::map<u32, std::vector<Decoration>>        decorations;
  std::map<u32, std::vector<Member_Decoration>> member_decorations;
  std::map<u32, FunTy>                          functypes;
  std::map<u32, MatrixTy>                       matrix_types;
  std::map<u32, StructTy>                       struct_types;
  std::map<u32, size_t>                         type_sizes;
  // function_id -> [var_id...]
  std::map<u32, std::vector<u32>> local_variables;
  std::vector<u32>                global_variables;
  // function_id -> [inst*...]
  std::map<u32, std::vector<u32 const *>>    instructions;
  std::map<u32, std::string>                 names;
  std::map<std::pair<u32, u32>, std::string> member_names;
  std::map<u32, Entry>                       entries;
  // Declaration order pairs
  std::vector<std::pair<u32, DeclTy>> decl_types;
  std::map<u32, DeclTy>               decl_types_table;
  // Offsets for private variables
  std::map<u32, u32> private_offsets;
  u32                private_storage_size = 0;
  // Offsets for input variables
  std::vector<u32>      input_sizes;
  std::vector<u32>      input_offsets;
  std::vector<VkFormat> input_formats;
  u32                   input_storage_size = 0;
  // Offsets for ouput variables
  std::vector<u32>      output_sizes;
  std::vector<u32>      output_offsets;
  std::vector<VkFormat> output_formats;
  u32                   output_storage_size = 0;
  // Lifetime must be long enough
  u32 const *   code;
  size_t        code_size;
  u64           code_hash;
  char          shader_dump_path[0x100];
  int ATTR_USED dump_spirv_module() const {
    FILE *file = fopen("shader_dump.spv", "wb");
    fwrite(code, 1, code_size * 4, file);
    fclose(file);
    return 0;
  }
  //////////////////////////////
  //          METHODS         //
  //////////////////////////////
  bool is_void_fn(u32 decl_id) {
    if (decl_types_table[decl_id] != DeclTy::Function) return false;
    Function function = functions[decl_id];
    return decl_types_table[function.result_type] == DeclTy::PrimitiveTy &&
           primitive_types[function.result_type].type == Primitive_t::Void;
  }
  auto has_decoration(spv::Decoration spv_dec, u32 val_id) -> bool {
    if (!contains(decorations, val_id)) return false;
    auto &decs = decorations[val_id];
    for (auto &dec : decs) {
      if (dec.type == spv_dec) return true;
    }
    return false;
  }
  auto find_decoration(spv::Decoration spv_dec, u32 val_id) -> Decoration {
    ASSERT_ALWAYS(contains(decorations, val_id));
    auto &decs = decorations[val_id];
    for (auto &dec : decs) {
      if (dec.type == spv_dec) return dec;
    }
    UNIMPLEMENTED;
  }
  auto has_member_decoration(spv::Decoration spv_dec, u32 type_id, u32 member_id) -> bool {
    ASSERT_ALWAYS(contains(member_decorations, type_id));
    for (auto &item : member_decorations[type_id]) {
      if (item.type == spv_dec && item.member_id == member_id) {
        return true;
      }
    }
    return false;
  }
  auto find_member_decoration(spv::Decoration spv_dec, u32 type_id, u32 member_id)
      -> Member_Decoration {
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
    using LLVM_IR_Builder_t         = llvm::IRBuilder<llvm::NoFolder>;
    auto &                        c = *context;
    llvm::SMDiagnostic            error;
    std::unique_ptr<llvm::Module> module = NULL;
    // Load the stdlib for a given subgroup size
    switch (opt_subgroup_size) {
    case 1: {
      auto mbuf = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef((char *)llvm_stdlib_1_bc, llvm_stdlib_1_bc_len), "", false);
      module = llvm::parseIR(*mbuf.get(), error, c);
      break;
    }
    case 4: {
      auto mbuf = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef((char *)llvm_stdlib_4_bc, llvm_stdlib_4_bc_len), "", false);
      module = llvm::parseIR(*mbuf.get(), error, c);
      break;
    }
    case 64: {
      auto mbuf = llvm::MemoryBuffer::getMemBuffer(
          llvm::StringRef((char *)llvm_stdlib_64_bc, llvm_stdlib_64_bc_len), "", false);
      module = llvm::parseIR(*mbuf.get(), error, c);
      break;
    }
    default: UNIMPLEMENTED;
    };
    ASSERT_ALWAYS(module);
    // Hide all stdlib functions
    for (llvm::Function &fun : module->functions()) {
      if (fun.isDeclaration()) continue;
      fun.setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
      fun.setVisibility(llvm::GlobalValue::VisibilityTypes::DefaultVisibility);
    }
    if (opt_llvm_dump) {
      std::vector<const char *> argv;
      argv.push_back("S2L");
      argv.push_back("--print-after-all");
      argv.push_back("--print-before-all");
      argv.push_back(NULL);
      llvm::cl::ParseCommandLineOptions(argv.size() - 1, &argv[0]);
    }
    llvm::PassRegistry *registry = llvm::PassRegistry::getPassRegistry();
    llvm::initializeCore(*registry);
    llvm::initializeScalarOpts(*registry);
    llvm::initializeIPO(*registry);
    llvm::initializeAnalysis(*registry);
    llvm::initializeTransformUtils(*registry);
    llvm::initializeInstCombine(*registry);
    llvm::initializeInstrumentation(*registry);
    llvm::initializeTarget(*registry);
    // Debug info builder
    std::unique_ptr<llvm::DIBuilder> llvm_di_builder;
    // those are removed along with dibuilder
    llvm::DICompileUnit *llvm_di_cuint;
    llvm::DIFile *       llvm_di_unit;
    if (opt_debug_info) {
      llvm_di_builder.reset(new llvm::DIBuilder(*module));
      // those are removed along with dibuilder
      llvm_di_cuint = llvm_di_builder->createCompileUnit(
          llvm::dwarf::DW_LANG_C, llvm_di_builder->createFile(shader_dump_path, "."), "SPRIV JIT",
          0, "", 0);
      llvm_di_unit = llvm_di_builder->createFile(shader_dump_path, ".");
    }
    //    std::map<llvm::Type *, llvm::DIType *> llvm_debug_type_table;
    //    std::function<llvm::DIType *(llvm::Type *)> llvm_get_debug_type =
    //        [&](llvm::Type *ty) {
    //          if (contains(llvm_debug_type_table, ty))
    //            return llvm_debug_type_table[ty];
    //          if (ty->isFloatTy()) {
    //            llvm::DIBasicType *bty = llvm_di_builder->createBasicType(
    //                "float", 32, llvm::dwarf::DW_ATE_float);
    //            llvm_debug_type_table[ty] = bty;
    //            return bty;
    //          } else if (ty->isVectorTy()) {
    //            llvm::VectorType *vtype =
    //            llvm::dyn_cast<llvm::VectorType>(ty); llvm::DIType *dity =
    //            llvm_di_builder->createVectorType(
    //                vtype->getElementCount(), vtype->getElementCount() * 4,
    //                llvm_get_deubg_type_table(vtype->getElementType()), {});
    //            "float", 32, llvm::dwarf::DW_ATE_float);
    //            llvm_debug_type_table[ty] = bty;
    //            return bty;
    //          } else {
    //            TRAP;
    //          }
    //        };

    /////////////////////////////
    llvm::install_fatal_error_handler(&llvm_fatal);
    auto llvm_get_constant_i32 = [&c](u32 a) {
      return llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(c), a);
    };
    auto llvm_get_constant_i64 = [&c](u64 a) {
      return llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(c), a);
    };
    const u32 *pCode = this->code;
    // The maximal number of IDs in this module
    const u32 ID_bound     = pCode[3];
    auto      get_spv_name = [this](u32 id) -> std::string {
      if (names.find(id) == names.end()) {
        names[id] = "spv_" + std::to_string(id);
      }
      ASSERT_ALWAYS(names.find(id) != names.end());
      return "spv_" + names[id];
    };
    auto llvm_vec_elem_type = [](llvm::Type *ty) {
      llvm::VectorType *vty = llvm::dyn_cast<llvm::VectorType>(ty);
      return vty->getElementType();
    };
    auto llvm_vec_num_elems = [](llvm::Type *ty) {
      llvm::VectorType *vty = llvm::dyn_cast<llvm::VectorType>(ty);
      return vty->getElementCount().Min;
    };
    auto llvm_matrix_transpose = [&](llvm::Value *matrix, LLVM_IR_Builder_t *llvm_builder) {
      llvm::Type *elem_type       = llvm_vec_elem_type(matrix->getType()->getArrayElementType());
      u32         matrix_row_size = llvm_vec_num_elems(matrix->getType()->getArrayElementType());
      u32         matrix_col_size = (u32)matrix->getType()->getArrayNumElements();
      llvm::Type *matrix_t_type =
          llvm::ArrayType::get(llvm::VectorType::get(elem_type, matrix_col_size), matrix_row_size);
      llvm::Value *                       result = llvm::UndefValue::get(matrix_t_type);
      llvm::SmallVector<llvm::Value *, 4> rows;
      jto(matrix_row_size) { rows.push_back(llvm_builder->CreateExtractValue(matrix, j)); }
      ito(matrix_col_size) {
        llvm::Value *result_row = llvm::UndefValue::get(matrix_t_type->getArrayElementType());
        jto(matrix_row_size) {
          result_row = llvm_builder->CreateInsertElement(
              result_row, llvm_builder->CreateExtractElement(rows[j], i), j);
        }
        result = llvm_builder->CreateInsertValue(result, result_row, i);
      }
      return result;
    };
    auto llvm_dot = [&](llvm::Value *vector_0, llvm::Value *vector_1,
                        LLVM_IR_Builder_t *llvm_builder) {
      llvm::Type * elem_type   = llvm_vec_elem_type(vector_0->getType());
      llvm::Value *result      = llvm::ConstantFP::get(elem_type, 0.0);
      u32          vector_size = llvm_vec_num_elems(vector_0->getType());
      ASSERT_ALWAYS(llvm_vec_num_elems(vector_1->getType()) ==
                    llvm_vec_num_elems(vector_0->getType()));
      jto(vector_size) {
        result = llvm_builder->CreateFAdd(
            result, llvm_builder->CreateFMul(llvm_builder->CreateExtractElement(vector_0, j),
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
    LOOKUP_FN(get_pixel_position);
    LOOKUP_FN(pixel_store_depth);
    LOOKUP_FN(dump_float4x4);
    LOOKUP_FN(dump_float4);
    LOOKUP_FN(dump_float3);
    LOOKUP_FN(dump_float2);
    LOOKUP_FN(dump_float);
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
    LOOKUP_FN(spv_fabs_f1);
    LOOKUP_FN(spv_fabs_f2);
    LOOKUP_FN(spv_fabs_f3);
    LOOKUP_FN(spv_fabs_f4);
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
    LOOKUP_FN(spv_is_front_face);
    //    LOOKUP_FN(spv_push_mask);
    //    LOOKUP_FN(spv_pop_mask);
    LOOKUP_FN(spv_get_lane_mask);
    LOOKUP_FN(spv_disable_lanes);
    LOOKUP_FN(spv_set_enabled_lanes);
    LOOKUP_FN(spv_get_enabled_lanes);
    LOOKUP_FN(spv_dummy_call);
    LOOKUP_FN(dump_mask);
    std::map<std::string, llvm::GlobalVariable *> global_strings;
    auto                                          lookup_string = [&](std::string str) {
      if (contains(global_strings, str)) return global_strings[str];
      llvm::Constant *      msg = llvm::ConstantDataArray::getString(c, str.c_str(), true);
      llvm::GlobalVariable *msg_glob = new llvm::GlobalVariable(
          *module, msg->getType(), true, llvm::GlobalValue::InternalLinkage, msg);
      global_strings[str] = msg_glob;
      return msg_glob;
    };
    auto lookup_image_op = [&](llvm::Type *res_type, llvm::Type *coord_type, bool read) {
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
      u32         dim           = 0;
      u32         components    = 0;
      llvm::Type *llvm_res_type = res_type;
      if (res_type->isVectorTy()) {
        components    = llvm_vec_num_elems(llvm_res_type);
        llvm_res_type = llvm_vec_elem_type(llvm_res_type);
      } else {
        components = 1;
      }
      if (coord_type->isVectorTy()) {
        dim = llvm_vec_num_elems(coord_type);
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
      ASSERT_ALWAYS(component_type == Primitive_t::F32 || component_type == Primitive_t::U32);
      u32         is_float = component_type == Primitive_t::F32 ? 1 : 0;
      char const *type_str = type_names[components * 2 + is_float];
      snprintf(tmp_buf, sizeof(tmp_buf), "spv_image_%s_%id_%s", (read ? "read" : "write"), dim,
               type_str);
      llvm::Function *fun = module->getFunction(tmp_buf);
      ASSERT_ALWAYS(fun != NULL);
      return fun;
    };
    llvm::Type *state_t = module->getTypeByName("struct.Invocation_Info");
    // Force 64 bit pointers
    llvm::Type *llvm_int_ptr_t = llvm::Type::getInt64Ty(c);
    llvm::Type *state_t_ptr    = llvm::PointerType::get(state_t, 0);
    llvm::Type *mask_t         = llvm::Type::getInt64Ty(c);
    // llvm::VectorType::get(llvm::IntegerType::getInt1Ty(c),
    //                     opt_subgroup_size);

    llvm::Type *sampler_t        = llvm::IntegerType::getInt64Ty(c);
    llvm::Type *image_t          = llvm::IntegerType::getInt64Ty(c);
    llvm::Type *combined_image_t = llvm::IntegerType::getInt64Ty(c);

    // Structure member offsets for GEP
    // SPIRV has ways of setting the member offset
    // So in LLVM IR we need to manually insert paddings
    std::map<llvm::Type *, std::vector<u32>> member_reloc;
    // Matrices could be row/column. here we just default to row major and do
    // the trasnpose as needed
    std::map<llvm::Type *, std::set<u32>> member_transpose;
    // Global type table
    std::vector<llvm::Type *> llvm_types(ID_bound);
    // Global value table (constants and global values)
    // Each function creates a copy that inherits the global
    // table
    std::vector<llvm::Value *> llvm_global_values(ID_bound);
    auto                       get_global_const_i32 = [&llvm_global_values](u32 const_id) {
      llvm::ConstantInt *co = llvm::dyn_cast<llvm::ConstantInt>(llvm_global_values[const_id]);
      NOTNULL(co);
      return (u32)co->getLimitedValue();
    };
    //    auto wave_local_t = [&](llvm::Type *ty) {
    //      return llvm::ArrayType::get(ty, opt_subgroup_size);
    //    };

    // Map spirv types to llvm types
    char name_buf[0x100];
    for (auto &item : this->decl_types) {
      ASSERT_ALWAYS(llvm_types[item.first] == NULL && "Types must have unique ids");
      ASSERT_ALWAYS(llvm_global_values[item.first] == NULL && "Values must have unique ids");
#define ASSERT_HAS(table) ASSERT_ALWAYS(table.find(item.first) != table.end());
      // Skip this declaration in this pass
      bool skip = false;
      switch (item.second) {
      case DeclTy::FunTy: {
        ASSERT_HAS(functypes);
        FunTy       type     = functypes.find(item.first)->second;
        llvm::Type *ret_type = llvm_types[type.ret];
        ASSERT_ALWAYS(ret_type != NULL && "Function must have a return type defined");
        llvm::SmallVector<llvm::Type *, 16> args;
        // Prepend state_t* to each function type
        args.push_back(state_t_ptr);
        // Prepend mask to each function type
        args.push_back(mask_t);
        for (auto &param_id : type.params) {
          llvm::Type *arg_type = llvm_types[param_id];
          ASSERT_ALWAYS(arg_type != NULL && "Function must have all argumet types defined");
          kto(opt_subgroup_size) args.push_back(arg_type);
        }
        if (ret_type->isVoidTy())
          llvm_types[type.id] = llvm::FunctionType::get(ret_type, args, false);
        else {
          // Append the pointer to the returned data as the last parameter
          kto(opt_subgroup_size) args.push_back(llvm::PointerType::get(ret_type, 0));
          //                    args.push_back(
          //              llvm::PointerType::get(llvm::ArrayType::get(ret_type, opt_subgroup_size),
          //              0));
          llvm_types[type.id] = llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false);
        }
        break;
      }
      case DeclTy::Function: {
        ASSERT_HAS(functions);
        Function            fun = functions.find(item.first)->second;
        llvm::FunctionType *fun_type =
            llvm::dyn_cast<llvm::FunctionType>(llvm_types[fun.function_type]);
        ASSERT_ALWAYS(fun_type != NULL && "Function type must be defined");
        llvm_global_values[fun.id] = llvm::Function::Create(
            fun_type, llvm::GlobalValue::ExternalLinkage, get_spv_name(fun.id), module.get());
        break;
      }
      case DeclTy::RuntimeArrayTy:
      case DeclTy::PtrTy: {
        ASSERT_HAS(ptr_types);
        PtrTy       type   = ptr_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.target_id];
        ASSERT_ALWAYS(elem_t != NULL && "Pointer target type must be defined");
        llvm_types[type.id] = llvm::PointerType::get(elem_t, 0);
        break;
      }
      case DeclTy::ArrayTy: {
        ASSERT_HAS(array_types);
        ArrayTy     type   = array_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.member_id];
        ASSERT_ALWAYS(elem_t != NULL && "Element type must be defined");
        llvm::Value *width_value = llvm_global_values[type.width_id];
        ASSERT_ALWAYS(width_value != NULL && "Array width must be defined");
        llvm::ConstantInt *constant = llvm::dyn_cast<llvm::ConstantInt>(width_value);
        ASSERT_ALWAYS(constant != NULL && "Array width must be an integer constant");
        llvm_types[type.id] = llvm::ArrayType::get(elem_t, constant->getValue().getLimitedValue());
        break;
      }
      case DeclTy::ImageTy: {
        ASSERT_HAS(images);
        ImageTy type        = images.find(item.first)->second;
        llvm_types[type.id] = image_t;
        break;
      }
      case DeclTy::Constant: {
        ASSERT_HAS(constants);
        Constant    c    = constants.find(item.first)->second;
        llvm::Type *type = llvm_types[c.type];
        ASSERT_ALWAYS(type != NULL && "Constant type must be defined");
        // In LLVM float means float 32bit
        if (!(type->isFloatTy()) && !(type->isIntegerTy() && (type->getIntegerBitWidth() == 32 ||
                                                              type->getIntegerBitWidth() == 1))) {
          UNIMPLEMENTED;
        }
        if (type->isFloatTy())
          llvm_global_values[c.id] = llvm::ConstantFP::get(type, llvm::APFloat(c.f32_val));
        else {
          llvm_global_values[c.id] = llvm::ConstantInt::get(type, c.i32_val);
        }
        break;
      }
      case DeclTy::ConstantComposite: {
        ASSERT_HAS(constants_composite);
        ConstantComposite c    = constants_composite.find(item.first)->second;
        llvm::Type *      type = llvm_types[c.type];
        ASSERT_ALWAYS(type != NULL && "Constant type must be defined");
        llvm::SmallVector<llvm::Constant *, 4> llvm_elems;
        ito(c.components.size()) {
          llvm::Value *   val  = llvm_global_values[c.components[i]];
          llvm::Constant *cnst = llvm::dyn_cast<llvm::Constant>(val);
          ASSERT_ALWAYS(cnst != NULL);
          llvm_elems.push_back(cnst);
        }
        if (type->isVectorTy())
          llvm_global_values[c.id] = llvm::ConstantVector::get(llvm_elems);
        else if (type->isArrayTy())
          llvm_global_values[c.id] =
              llvm::ConstantArray::get(llvm::dyn_cast<llvm::ArrayType>(type), llvm_elems);
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
        default: UNIMPLEMENTED_(get_cstr(c.storage));
        }
        break;
      }
      case DeclTy::MatrixTy: {
        ASSERT_HAS(matrix_types);
        MatrixTy    type   = matrix_types.find(item.first)->second;
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
          llvm::Type *struct_type =
              llvm::StructType::create(c, {llvm::VectorType::get(llvm::Type::getFloatTy(c), 4)},
                                       get_spv_name(type.id), true);
          member_reloc[struct_type] = {0};
          llvm_types[type.id]       = struct_type;
          break;
        }
        std::vector<llvm::Type *> members;
        size_t                    offset = 0;
        // We manually insert padding bytes which offsets the structure members
        // for GEP instructions
        std::vector<u32> member_indices;
        std::set<u32>    this_member_transpose;
        u32              index_offset = 0;
        for (u32 member_id = 0; member_id < type.member_types.size(); member_id++) {
          llvm::Type *member_type = llvm_types[type.member_types[member_id]];
          ASSERT_ALWAYS(member_type != NULL && "Member types must be defined");
          if (!type.is_builtin && type.member_offsets[member_id] != offset) {
            ASSERT_ALWAYS(type.member_offsets[member_id] > offset &&
                          "Can't move a member back in memory layout");
            size_t diff = type.member_offsets[member_id] - offset;
            // Push dummy bytes until the member offset is ok
            members.push_back(llvm::ArrayType::get(llvm::Type::getInt8Ty(c), diff));
            index_offset += 1;
          }
          if (has_member_decoration(spv::Decoration::DecorationColMajor, type.id, member_id)) {
            this_member_transpose.insert(member_id);
            if (has_member_decoration(spv::Decoration::DecorationMatrixStride, type.id,
                                      member_id)) {
              // do we need to change anything if that's not true?
              // TODO allow 12 and 8 for mat3 and mat2
              ASSERT_ALWAYS(find_member_decoration(spv::Decoration::DecorationMatrixStride, type.id,
                                                   member_id)
                                .param1 == 16);
            }
          }
          size_t size           = 0;
          u32    member_type_id = type.member_types[member_id];
          member_indices.push_back(index_offset);
          index_offset += 1;
          ASSERT_ALWAYS(contains(type_sizes, member_type_id));
          size = type_sizes[member_type_id];
          ASSERT_ALWAYS(size != 0);
          offset += size;
          members.push_back(member_type);
        }
        llvm::Type *struct_type = llvm::StructType::create(c, members, get_spv_name(type.id), true);
        member_reloc[struct_type] = member_indices;
        if (this_member_transpose.size() != 0)
          member_transpose[struct_type] = this_member_transpose;
        llvm_types[type.id] = struct_type;
        break;
      }
      case DeclTy::VectorTy: {
        ASSERT_HAS(vector_types);
        VectorTy    type   = vector_types.find(item.first)->second;
        llvm::Type *elem_t = llvm_types[type.member_id];
        ASSERT_ALWAYS(elem_t != NULL && "Element type must be defined");
        llvm_types[type.id] = llvm::VectorType::get(elem_t, type.width);
        break;
      }
      case DeclTy::SamplerTy: {
        ASSERT_HAS(samplers);
        SamplerTy type      = samplers.find(item.first)->second;
        llvm_types[type.id] = sampler_t;
        break;
      }
      case DeclTy::Sampled_ImageTy: {
        ASSERT_HAS(combined_images);
        Sampled_ImageTy type = combined_images.find(item.first)->second;
        llvm_types[type.id]  = combined_image_t;
        break;
      }
      case DeclTy::PrimitiveTy: {
        ASSERT_HAS(primitive_types);
        PrimitiveTy type = primitive_types.find(item.first)->second;
        switch (type.type) {
#define MAP(TY, LLVM_TY)                                                                           \
  case Primitive_t::TY: llvm_types[type.id] = LLVM_TY; break;
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
        default: UNIMPLEMENTED;
        }
        break;
      }
      default: UNIMPLEMENTED_(get_cstr(item.second));
      }
      if (skip) continue;
      ASSERT_ALWAYS((llvm_global_values[item.first] != NULL || llvm_types[item.first] != NULL) &&
                    "eh there must be a type or value at the end!");
    }
    auto get_fn_ty = [&](llvm::Value *fn) {
      llvm::PointerType *ptr_ty = llvm::dyn_cast<llvm::PointerType>(fn->getType());
      NOTNULL(ptr_ty);
      return llvm::dyn_cast<llvm::FunctionType>(ptr_ty->getPointerElementType());
    };
    // Second pass:
    // Emit instructions
    for (auto &item : instructions) {
      // Control flow specific tracking
      struct BranchCond {
        llvm::BasicBlock *bb;
        u32               cond_id;
        u32               true_id;
        u32               false_id;
        u32               merge_id;
        int32_t           continue_id;
      };
      struct DeferredStore {
        llvm::Value *dst_ptr;
        llvm::Value *value_ptr;
      };
      int32_t cur_merge_id    = -1;
      int32_t cur_continue_id = -1;
      struct DeferredPhi {
        llvm::BasicBlock *                        bb;
        llvm::SmallVector<std::pair<u32, u32>, 4> vars;
        llvm::SmallVector<llvm::Value *, 64>      dummy_values;
        llvm::Type *                              result_type;
      };
      std::vector<BranchCond> deferred_branches;
      //      std::vector<std::pair<llvm::BasicBlock *, u32>> deferred_jumps;
      std::vector<DeferredStore>                             deferred_stores;
      std::map<llvm::BasicBlock *, std::vector<DeferredPhi>> deferred_phis;
      std::map<u32, llvm::BasicBlock *>                      llvm_labels;

      std::vector<llvm::BasicBlock *> bbs;
      std::set<llvm::Value *>         conditions;
      std::set<llvm::BasicBlock *>    terminators;
      std::set<llvm::BasicBlock *>    unreachables;
      llvm::BasicBlock *              entry_bb = NULL;

      ///////////////////////////////////////////////
      u32      func_id         = item.first;
      Function function        = functions[func_id];
      bool     is_entry        = contains(entries, func_id);
      bool     is_pixel_shader = is_entry && entries[func_id].execution_model ==
                                             spv::ExecutionModel::ExecutionModelFragment;
      NOTNULL(llvm_global_values[func_id]);
      llvm::Function *cur_fun = llvm::dyn_cast<llvm::Function>(llvm_global_values[func_id]);
      NOTNULL(cur_fun);
      llvm::SmallVector<llvm::Value *, 64> returned_values;
      llvm::FunctionType *                 cur_fun_ty = get_fn_ty(cur_fun);
      if (!is_void_fn(func_id)) {
        // Usually it's an array
        // each parameter is duplicated opt_subgroup_size times + state + mask then returned valuse
        kto(opt_subgroup_size) returned_values.push_back(
            cur_fun->getArg(function.params.size() * opt_subgroup_size + 2 + k));
      }
      llvm::Value *state_ptr = cur_fun->getArg(0);
      state_ptr->setName("state_ptr");
      // Check that the first parameter is state_t*
      ASSERT_ALWAYS(state_ptr->getType() == state_t_ptr);
      // @llvm/allocas
      llvm::BasicBlock *cur_bb     = llvm::BasicBlock::Create(c, "allocas", cur_fun, NULL);
      llvm::BasicBlock *allocas_bb = cur_bb;

      std::unique_ptr<LLVM_IR_Builder_t> llvm_builder;
      llvm_builder.reset(new LLVM_IR_Builder_t(cur_bb, llvm::NoFolder()));
      llvm::Value *mask_register = llvm_builder->CreateAlloca(mask_t, NULL, "mask_register");
      std::map<llvm::BasicBlock *, llvm::Value *> mask_registers;
      mask_registers[allocas_bb] = mask_register;
      llvm_builder->CreateStore(cur_fun->getArg(1), mask_register);
      auto get_lane_mask_bit = [&](u32 lane_id) {
        llvm::Value *mask = llvm_builder->CreateLoad(mask_register);
        return llvm_builder->CreateCall(spv_get_lane_mask,
                                        {state_ptr, mask, llvm_get_constant_i32(lane_id)});
        //          return llvm::ConstantInt::get(llvm::Type::getInt1Ty(c), 1);
      };
      auto i1_vec_to_mask = [&](llvm::Value *i1vec) {
        llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(i1vec->getType());
        NOTNULL(vtype);
        ASSERT_ALWAYS(vtype->getElementType() == llvm::Type::getInt1Ty(c));
        llvm::Value *bitcast =
            llvm_builder->CreateBitCast(i1vec, llvm::Type::getIntNTy(c, vtype->getNumElements()));
        return llvm_builder->CreateZExt(bitcast, llvm::Type::getInt64Ty(c));
      };
      auto llvm_print_string = [&](char const *str) {
        llvm::Value *str_const = lookup_string(str);
        llvm_builder->CreateCall(
            dump_string,
            {state_ptr, llvm_builder->CreateBitCast(str_const, llvm::Type::getInt8PtrTy(c))});
      };

      auto masked_store = [&](llvm::SmallVector<llvm::Value *, 64> &values,
                              llvm::SmallVector<llvm::Value *, 64> &addresses,
                              llvm::BasicBlock *                    bb) {
        //        llvm_print_string("mask:");
        //        llvm_builder->CreateCall(dump_mask, {state_ptr,
        //        llvm_builder->CreateLoad(mask_register)});
        kto(opt_subgroup_size) {
          llvm::Value *old_value =
              new llvm::LoadInst(values[k]->getType(), addresses[k], "old_value", bb);
          llvm::Value *mask = new llvm::LoadInst(mask_t, mask_registers[bb], "mask_register", bb);
          //        lane_bit llvm_builder->CreateCall(spv_get_lane_mask,
          //                                        {state_ptr, mask,
          //                                        llvm_get_constant_i32(lane_id)});

          //          llvm::Value *lane_bit = get_lane_mask_bit(k);
          llvm::Value *lane_bit = llvm::CallInst::Create(
              spv_get_lane_mask, {state_ptr, mask, llvm_get_constant_i32(k)}, "lane_bit", bb);
          llvm::Value *select =
              llvm::SelectInst::Create(lane_bit, values[k], old_value, "select", bb);
          new llvm::StoreInst(select, addresses[k], bb);
        }
      };
      auto save_enabled_lanes_mask = [&]() {
        llvm::Value *mask =
            llvm_builder->CreateCall(spv_get_enabled_lanes, {state_ptr}, "enabled_lanes");
        llvm::Value *alloca = llvm_builder->CreateAlloca(mask_t);
        llvm_builder->CreateStore(mask, alloca);
        return alloca;
      };
      auto restore_enabled_lanes_mask = [&](llvm::Value *alloca) {
        llvm::Value *mask = llvm_builder->CreateLoad(alloca);
        llvm_builder->CreateCall(spv_set_enabled_lanes, {state_ptr, mask});
      };

      // Debug line number
      u32            cur_spirv_line = function.spirv_line;
      llvm::DIScope *di_scope       = NULL;
      if (opt_debug_info) {
        llvm::DISubprogram *SP = llvm_di_builder->createFunction(
            (llvm::DIScope *)llvm_di_unit, "@Function", llvm::StringRef(), llvm_di_unit,
            cur_spirv_line, llvm_di_builder->createSubroutineType({}), 0,
            llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagDefinition);
        cur_fun->setSubprogram(SP);
        di_scope = SP;
        llvm_builder->SetCurrentDebugLocation(llvm::DebugLoc::get(cur_spirv_line, 0, di_scope));
      }
      // TODO(aschrein) maybe do something more optimal?
      // Each lane has it's own value table(quick and dirty way but will help me
      // advance)
      std::vector<std::vector<llvm::Value *>> llvm_values_per_lane(opt_subgroup_size);
      // Copy global constants into each lane
      ito(opt_subgroup_size) llvm_values_per_lane[i] = copy(llvm_global_values);
      // Setup arguments
      u32 cur_param_id = 2;

      for (auto &param : function.params) {
        u32 res_type_id = param.type_id;
        u32 res_id      = param.id;
        kto(opt_subgroup_size) {
          llvm_values_per_lane[k][res_id] = cur_fun->getArg(cur_param_id++);
        }
      }

      // Call to get input/output/uniform data
      llvm::Value *input_ptr          = llvm_builder->CreateCall(get_input_ptr, state_ptr);
      llvm::Value *output_ptr         = llvm_builder->CreateCall(get_output_ptr, state_ptr);
      llvm::Value *builtin_output_ptr = llvm_builder->CreateCall(get_builtin_output_ptr, state_ptr);
      llvm::SmallVector<llvm::Value *, 64> barycentrics;
      llvm::SmallVector<llvm::Value *, 64> pixel_positions;
      if (is_pixel_shader) {
        ito(opt_subgroup_size) {
          llvm::Value *b =
              llvm_builder->CreateCall(get_barycentrics, {state_ptr, llvm_get_constant_i32(i)});
          barycentrics.push_back(b);
          llvm::Value *b_0 = llvm_builder->CreateExtractElement(b, (u64)0);
          llvm::Value *b_1 = llvm_builder->CreateExtractElement(b, (u64)1);
          llvm::Value *b_2 = llvm_builder->CreateExtractElement(b, (u64)2);
          llvm::Value *pos = llvm_builder->CreateCall(
              get_pixel_position, {state_ptr, llvm_get_constant_i32(i), b_0, b_1, b_2});
          pixel_positions.push_back(pos);
          llvm::Value *depth = llvm_builder->CreateExtractElement(pos, (u64)2);
          llvm_builder->CreateCall(pixel_store_depth, {state_ptr, llvm_get_constant_i32(i), depth});
        }
      }
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
        llvm::PointerType *ptr_type = llvm::dyn_cast<llvm::PointerType>(llvm_type);
        NOTNULL(ptr_type);

        llvm::Type *pointee_type = ptr_type->getElementType();
        ito(opt_subgroup_size) {
          llvm::Value *llvm_value =
              llvm_builder->CreateAlloca(pointee_type, 0, NULL, get_spv_name(var.id));
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
        PtrTy  ptr_type        = ptr_types[var.type_id];
        DeclTy pointee_decl_ty = decl_types_table[ptr_type.target_id];

        bool     is_builtin_struct = false;
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
        u32         num_components = 0;
        Primitive_t component_ty   = Primitive_t::Void;
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
          component_ty   = primitive_types[vec_ty.member_id].type;
          num_components = vec_ty.width;
          break;
        }
        default: break;
        }
        // Handle each storage type differently
        switch (var.storage) {
        case spv::StorageClass::StorageClassPrivate: {
          ito(opt_subgroup_size) {
            llvm::Value *pc_ptr = llvm_builder->CreateCall(get_private_ptr, state_ptr);
            ASSERT_ALWAYS(contains(private_offsets, var_id));
            u32          offset = private_storage_size * i + private_offsets[var_id];
            llvm::Value *pc_ptr_offset =
                llvm_builder->CreateGEP(pc_ptr, llvm::ConstantInt::get(c, llvm::APInt(32, offset)));
            llvm::Value *llvm_value =
                llvm_builder->CreateBitCast(pc_ptr_offset, llvm_type, get_spv_name(var.id));
            llvm_values_per_lane[i][var.id] = llvm_value;
          }
          break;
        }
        case spv::StorageClass::StorageClassUniformConstant:
        case spv::StorageClass::StorageClassStorageBuffer:
        case spv::StorageClass::StorageClassUniform: {
          u32 set     = find_decoration(spv::Decoration::DecorationDescriptorSet, var.id).param1;
          u32 binding = find_decoration(spv::Decoration::DecorationBinding, var.id).param1;
          ASSERT_ALWAYS(set >= 0);
          ASSERT_ALWAYS(binding >= 0);
          llvm::Value *pc_ptr = llvm_builder->CreateCall(

              var.storage == spv::StorageClass::StorageClassUniformConstant
                  ? get_uniform_const_ptr
                  : var.storage == spv::StorageClass::StorageClassUniform ? get_uniform_ptr
                                                                          : get_storage_ptr,
              {state_ptr, llvm_builder->getInt32((u32)set), llvm_builder->getInt32((u32)binding)});
          llvm::Value *llvm_value =
              llvm_builder->CreateBitCast(pc_ptr, llvm_type, get_spv_name(var.id));
          ito(opt_subgroup_size) { llvm_values_per_lane[i][var.id] = llvm_value; }
          break;
        }
        case spv::StorageClass::StorageClassOutput: {
          if (is_builtin_struct) {
            llvm::ArrayType *array_type =
                llvm::ArrayType::get(llvm_type->getPointerElementType(), opt_subgroup_size);
            llvm::Value * alloca = llvm_builder->CreateAlloca(array_type);
            DeferredStore ds;
            ds.dst_ptr   = builtin_output_ptr;
            ds.value_ptr = alloca;
            deferred_stores.push_back(ds);
            ito(opt_subgroup_size) {
              llvm::Value *offset = llvm_builder->CreateGEP(
                  alloca, {llvm_get_constant_i32(0), llvm_get_constant_i32(i)},
                  get_spv_name(var.id));

              llvm_values_per_lane[i][var.id] = offset;
            }
          } else {
            u32 location = find_decoration(spv::Decoration::DecorationLocation, var.id).param1;
            ASSERT_ALWAYS(location >= 0);
            ASSERT_ALWAYS(llvm_type->isPointerTy());
            ito(opt_subgroup_size) {
              llvm::Value * alloca = llvm_builder->CreateAlloca(llvm_type->getPointerElementType());
              DeferredStore ds;
              ds.dst_ptr = llvm_builder->CreateGEP(
                  output_ptr,
                  llvm_get_constant_i32(output_offsets[location] + output_storage_size * i));
              ds.value_ptr = alloca;
              deferred_stores.push_back(ds);
              llvm_values_per_lane[i][var.id] = alloca;
            }
          }
          break;
        }
        case spv::StorageClass::StorageClassInput: {
          // Builtin variable: special case
          if (has_decoration(spv::Decoration::DecorationBuiltIn, var.id)) {
            spv::BuiltIn builtin_id =
                (spv::BuiltIn)find_decoration(spv::Decoration::DecorationBuiltIn, var.id).param1;
            switch (builtin_id) {
            case spv::BuiltIn::BuiltInGlobalInvocationId: {
              llvm::VectorType *gid_t = llvm::VectorType::get(llvm::IntegerType::getInt32Ty(c), 3);
              ito(opt_subgroup_size) {
                llvm::Value *alloca = llvm_builder->CreateAlloca(gid_t, NULL, get_spv_name(var.id));
                llvm::Value *gid    = llvm_builder->CreateCall(spv_get_global_invocation_id,
                                                            {state_ptr, llvm_get_constant_i32(i)});
                llvm_builder->CreateStore(gid, alloca);
                llvm_values_per_lane[i][var.id] = alloca;
              }

              break;
            }
            case spv::BuiltIn::BuiltInWorkgroupSize: {
              llvm::VectorType *gid_t = llvm::VectorType::get(llvm::IntegerType::getInt32Ty(c), 3);
              llvm::Value *alloca = llvm_builder->CreateAlloca(gid_t, NULL, get_spv_name(var.id));
              llvm::Value *gid    = llvm_builder->CreateCall(spv_get_work_group_size, {state_ptr});
              llvm_builder->CreateStore(gid, alloca);
              ito(opt_subgroup_size) { llvm_values_per_lane[i][var.id] = alloca; }
              break;
            }
            case spv::BuiltIn::BuiltInFrontFacing: {
              ito(opt_subgroup_size) {
                llvm::Value *alloca = llvm_builder->CreateAlloca(llvm::Type::getInt1Ty(c), NULL,
                                                                 get_spv_name(var.id));
                llvm_builder->CreateStore(
                    llvm_builder->CreateCall(spv_is_front_face,
                                             {state_ptr, llvm_get_constant_i32(i)}),
                    alloca);
                llvm_values_per_lane[i][var.id] = alloca;
              }
              break;
            }
            default: UNIMPLEMENTED_(get_cstr(builtin_id));
            }
            break;
          }
          // Don't emit pipeline input variables for local functions because for
          // pixel shaders we need to interpolate and we don't know if a
          // function is called from a pixel shader of vertex shader
          if (!is_entry) break;
          // Pipeline input
          ASSERT_ALWAYS((pointee_decl_ty == DeclTy::PrimitiveTy &&
                         primitive_types[ptr_type.target_id].type == Primitive_t::F32) ||
                        (pointee_decl_ty == DeclTy::VectorTy &&
                         vector_types[ptr_type.target_id].width <= 4) ||
                        false);
          u32 location = find_decoration(spv::Decoration::DecorationLocation, var.id).param1;
          ASSERT_ALWAYS(location >= 0);
          // For now just stupid array of structures
          ito(opt_subgroup_size) {
            // For pixel shader we need to interpolate pipeline inputs
            if (is_pixel_shader) {
              llvm::Value *b_0   = llvm_builder->CreateExtractElement(barycentrics[i], (u64)0);
              llvm::Value *b_1   = llvm_builder->CreateExtractElement(barycentrics[i], (u64)1);
              llvm::Value *b_2   = llvm_builder->CreateExtractElement(barycentrics[i], (u64)2);
              llvm::Value *gep_0 = llvm_builder->CreateGEP(
                  input_ptr, llvm_get_constant_i32(input_storage_size * (i * 3 + 0) +
                                                   input_offsets[location]));
              llvm::Value *gep_1 = llvm_builder->CreateGEP(
                  input_ptr, llvm_get_constant_i32(input_storage_size * (i * 3 + 1) +
                                                   input_offsets[location]));
              llvm::Value *gep_2 = llvm_builder->CreateGEP(
                  input_ptr, llvm_get_constant_i32(input_storage_size * (i * 3 + 2) +
                                                   input_offsets[location]));
              llvm::Value *bitcast_0 = llvm_builder->CreateBitCast(gep_0, llvm_type);
              llvm::Value *bitcast_1 = llvm_builder->CreateBitCast(gep_1, llvm_type);
              llvm::Value *bitcast_2 = llvm_builder->CreateBitCast(gep_2, llvm_type);
              llvm::Value *val_0     = llvm_builder->CreateLoad(bitcast_0);
              llvm::Value *val_1     = llvm_builder->CreateLoad(bitcast_1);
              llvm::Value *val_2     = llvm_builder->CreateLoad(bitcast_2);
              // Debug
              if (0) {
                {
                  llvm_builder->CreateCall(dump_float, {state_ptr, b_0});
                  llvm_builder->CreateCall(dump_float, {state_ptr, b_1});
                  llvm_builder->CreateCall(dump_float, {state_ptr, b_2});
                }
                if (val_0->getType()->isVectorTy()) {
                  u32 num_elems = llvm_vec_num_elems(val_0->getType());
                  if

                      (num_elems == 3) {
                    llvm::Value *reinterpret =
                        llvm_builder->CreateBitCast(bitcast_0, llvm::Type::getFloatPtrTy(c));
                    llvm_builder->CreateCall(dump_float3, {state_ptr, reinterpret});
                  }
                }
              }
              // TODO: handle more types/flat interpolation later
              ASSERT_ALWAYS(val_0->getType()->isVectorTy() || val_0->getType()->isFloatTy());
              if (val_0->getType()->isVectorTy()) {
                b_0 = llvm_builder->CreateVectorSplat(llvm_vec_num_elems(val_0->getType()), b_0);
                b_1 = llvm_builder->CreateVectorSplat(llvm_vec_num_elems(val_0->getType()), b_1);
                b_2 = llvm_builder->CreateVectorSplat(llvm_vec_num_elems(val_0->getType()), b_2);
              }
              val_0 = llvm_builder->CreateFMul(val_0, b_0);
              val_1 = llvm_builder->CreateFMul(val_1, b_1);
              val_2 = llvm_builder->CreateFMul(val_2, b_2);
              llvm::Value *final_val =
                  llvm_builder->CreateFAdd(val_0, llvm_builder->CreateFAdd(val_1, val_2));
              llvm::Value *alloca = llvm_builder->CreateAlloca(val_0->getType());
              llvm_builder->CreateStore(final_val, alloca);
              llvm_values_per_lane[i][var.id] = alloca;
            } else { // Just load raw input
              llvm::Value *gep = llvm_builder->CreateGEP(
                  input_ptr,
                  llvm_get_constant_i32(input_storage_size * i + input_offsets[location]));
              llvm::Value *bitcast            = llvm_builder->CreateBitCast(gep, llvm_type);
              llvm_values_per_lane[i][var.id] = bitcast;
            }
          }
          break;
        }
        case spv::StorageClass::StorageClassPushConstant: {
          llvm::Value *pc_ptr = llvm_builder->CreateCall(get_push_constant_ptr, state_ptr);
          llvm::Value *llvm_value =
              llvm_builder->CreateBitCast(pc_ptr, llvm_type, get_spv_name(var.id));
          ito(opt_subgroup_size) { llvm_values_per_lane[i][var.id] = llvm_value; }
          break;
        }
        default: UNIMPLEMENTED_(get_cstr(var.storage));
        }

        if (var.init_id != 0) {
          UNIMPLEMENTED;
        }
      }
      for (u32 const *pCode : item.second) {
        u16     WordCount = pCode[0] >> spv::WordCountShift;
        spv::Op opcode    = spv::Op(pCode[0] & spv::OpCodeMask);
        u32     word1     = pCode[1];
        u32     word2     = WordCount > 2 ? pCode[2] : 0;
        u32     word3     = WordCount > 3 ? pCode[3] : 0;
        u32     word4     = WordCount > 4 ? pCode[4] : 0;
        u32     word5     = WordCount > 5 ? pCode[5] : 0;
        u32     word6     = WordCount > 6 ? pCode[6] : 0;
        u32     word7     = WordCount > 7 ? pCode[7] : 0;
        u32     word8     = WordCount > 8 ? pCode[8] : 0;
        u32     word9     = WordCount > 9 ? pCode[9] : 0;
        if (cur_bb != NULL && opt_debug_comments) {
          static char                str_buf[0x100];
          std::vector<llvm::Type *>  AsmArgTypes;
          std::vector<llvm::Value *> AsmArgs;
          snprintf(str_buf, sizeof(str_buf), "%s: word1: %i word2: %i", get_cstr((spv::Op)opcode),
                   word1, word2);
          llvm::FunctionType *AsmFTy =
              llvm::FunctionType::get(llvm::Type::getVoidTy(c), AsmArgTypes, false);
          llvm::InlineAsm *IA =
              llvm::InlineAsm::get(AsmFTy, str_buf, "", true,
                                   /* IsAlignStack */ false, llvm::InlineAsm::AD_ATT);
          llvm::CallInst::Create(IA, AsmArgs, "", cur_bb);
        }
        cur_spirv_line++;
        if (cur_bb != NULL && opt_debug_info) {
          llvm_builder->SetCurrentDebugLocation(llvm::DebugLoc::get(cur_spirv_line, 0, di_scope));
        }

        switch (opcode) {
        case spv::Op::OpLabel: {
          u32 id = word1;
          snprintf(name_buf, sizeof(name_buf), "label_%i", id);
          llvm::BasicBlock *new_bb = llvm::BasicBlock::Create(c, name_buf);
          new_bb->insertInto(cur_fun);
          if (cur_bb != NULL) {
            ASSERT_ALWAYS(entry_bb == NULL);
            entry_bb = new_bb;
            //            deferred_branches.push_back({.bb          = cur_bb,
            //                                         .cond_id     = 0,
            //                                         .true_id     = id,
            //                                         .false_id    = 0,
            //                                         .merge_id    = 0,
            //                                         .continue_id = 0});
            //                        llvm::BranchInst::Create(new_bb, cur_bb);
          }
          cur_bb = new_bb;
          bbs.push_back(new_bb);
          llvm_builder.reset(new LLVM_IR_Builder_t(cur_bb, llvm::NoFolder()));
          llvm_labels[id] = cur_bb;
          mask_register   = new llvm::AllocaInst(mask_t, 0, "mask_register", allocas_bb);
          new llvm::StoreInst(llvm_get_constant_i64(0), mask_register, allocas_bb);
          mask_registers[cur_bb] = mask_register;
          break;
        }
        case spv::Op::OpAccessChain: {
          ASSERT_ALWAYS(cur_bb != NULL);
          kto(opt_subgroup_size) {
            llvm::Value *base = llvm_values_per_lane[k][word3];
            ASSERT_ALWAYS(base != NULL);

            std::vector<llvm::Value *> indices = {};

            for (u32 i = 4; i < WordCount; i++) {
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
              llvm::Type *pointee_type = val->getType()->getPointerElementType();
              NOTNULL(pointee_type);
              if (pointee_type->isStructTy()) {
                llvm::StructType *struct_type = llvm::dyn_cast<llvm::StructType>(pointee_type);
                NOTNULL(struct_type);
                llvm::ConstantInt *integer = llvm::dyn_cast<llvm::ConstantInt>(index_val);
                ASSERT_ALWAYS(integer != NULL &&
                              "Access chain index must be OpConstant for structures");
                u32  cval             = (u32)integer->getLimitedValue();
                u32  struct_member_id = cval;
                bool transpose        = false;
                if (contains(member_transpose, struct_type) &&
                    contains(member_transpose[struct_type], struct_member_id)) {
                  transpose = true;
                }
                if (contains(member_reloc, pointee_type)) {
                  std::vector<u32> const &reloc_table = member_reloc[pointee_type];
                  struct_member_id                    = reloc_table[cval];
                }
                llvm::Type *member_type = struct_type->getElementType(struct_member_id);

                val = llvm_builder->CreateGEP(
                    val,
                    {llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), (u32)0),
                     llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), (u32)struct_member_id)});
                if (transpose) {
                  llvm::Value *alloca = llvm_builder->CreateAlloca(member_type);
                  llvm_builder->CreateStore(
                      llvm_matrix_transpose(llvm_builder->CreateLoad(val), llvm_builder.get()),
                      alloca);
                  val = alloca;
                }
                // Make sure there is no reinterpretation at SPIRV level
                ASSERT_ALWAYS(i != indices.size() - 1 || result_type == val->getType());
              } else {
                if (i == indices.size() - 1) {
                  llvm::Value *reinter = llvm_builder->CreateBitCast(val, result_type);
                  llvm::Value *gep     = llvm_builder->CreateGEP(reinter, index_val);
                  val                  = gep;
                } else {
                  llvm::Value *gep = llvm_builder->CreateGEP(val, index_val);
                  val              = gep;
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
          llvm::SmallVector<llvm::Value *, 64> values;
          llvm::SmallVector<llvm::Value *, 64> addresses;
          kto(opt_subgroup_size) {
            llvm::Value *addr = llvm_values_per_lane[k][word1];
            ASSERT_ALWAYS(addr != NULL);
            llvm::Value *val = llvm_values_per_lane[k][word2];
            ASSERT_ALWAYS(val != NULL);
            //            llvm_builder->CreateStore(val, addr);
            values.push_back(val);
            addresses.push_back(addr);
          }
          masked_store(values, addresses, cur_bb);
          break;
        }
        case spv::Op::OpLoopMerge: {
          cur_merge_id    = (int32_t)word1;
          cur_continue_id = (int32_t)word2;
          break;
        }
        case spv::Op::OpSelectionMerge: {
          cur_merge_id = (int32_t)word1;
          break;
        }
        case spv::Op::OpSwitch: {
          // Only trivial switches are supported
          ASSERT_ALWAYS(WordCount == 3);
          u32 selector_id = word1;
          u32 default_id  = word2;
          // branches reference labels that haven't been created yet
          // So we just fix this up later
          deferred_branches.push_back({.bb          = cur_bb,
                                       .cond_id     = 0,
                                       .true_id     = default_id,
                                       .false_id    = 0,
                                       .merge_id    = 0,
                                       .continue_id = 0});
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          mask_register = NULL;
          break;
        }
        case spv::Op::OpBranch: {
          // branches reference labels that haven't been created yet
          // So we just fix this up later
          deferred_branches.push_back({.bb          = cur_bb,
                                       .cond_id     = 0,
                                       .true_id     = word1,
                                       .false_id    = 0,
                                       .merge_id    = 0,
                                       .continue_id = 0});
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          mask_register = NULL;
          break;
        }
        case spv::Op::OpBranchConditional: {
          ASSERT_ALWAYS(cur_merge_id >= 0);
          BranchCond bc;
          bc.bb          = cur_bb;
          bc.cond_id     = word1;
          bc.true_id     = word2;
          bc.false_id    = word3;
          bc.merge_id    = (u32)cur_merge_id;
          bc.continue_id = cur_continue_id;
          // branches reference labels that haven't been created yet
          // So we just fix this up later
          deferred_branches.push_back(bc);
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          cur_merge_id    = -1;
          cur_continue_id = -1;
          mask_register   = NULL;
          kto(opt_subgroup_size) { conditions.insert(llvm_values_per_lane[k][word1]); }
          break;
        }
        case spv::Op::OpGroupNonUniformBallot: {
          ASSERT_ALWAYS(WordCount == 5);
          u32                res_type_id = word1;
          u32                res_id      = word2;
          u32                scope_id    = word3;
          llvm::Value *      scope_val   = llvm_global_values[scope_id];
          llvm::ConstantInt *scope_const = llvm::dyn_cast<llvm::ConstantInt>(scope_val);
          NOTNULL(scope_const);
          u32 scope        = (u32)scope_const->getLimitedValue();
          u32 predicate_id = word4;
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Type *result_type = llvm_types[res_type_id];
          ASSERT_ALWAYS(result_type->isVectorTy() && llvm_vec_num_elems(result_type) == 4 &&
                        llvm_vec_elem_type(result_type) == llvm::IntegerType::getInt32Ty(c));
          llvm::Value *result_value = llvm_builder->CreateVectorSplat(4, llvm_get_constant_i32(0));
          ASSERT_ALWAYS(opt_subgroup_size <= 64);
          llvm::Value *result_elem_0 =
              llvm_builder->CreateAnd(llvm_get_constant_i32(0), llvm_get_constant_i32(0));
          llvm::Value *result_elem_1 =
              llvm_builder->CreateAnd(llvm_get_constant_i32(0), llvm_get_constant_i32(0));
          kto(opt_subgroup_size) {
            llvm::Value *pred_val  = llvm_values_per_lane[k][predicate_id];
            llvm::Value *lane_mask = llvm_builder->CreateExtractElement(mask_register, k);
            llvm::Value *and_val   = llvm_builder->CreateAnd(pred_val, lane_mask);
            llvm::Value *zext = llvm_builder->CreateZExt(and_val, llvm::IntegerType::getInt32Ty(c));
            if (k < 32) {
              llvm::Value *shift = llvm_builder->CreateShl(zext, llvm::APInt(32, k));
              result_elem_0      = llvm_builder->CreateOr(shift, result_elem_0);
            } else {
              llvm::Value *shift = llvm_builder->CreateShl(zext, llvm::APInt(32, k - 32));
              result_elem_1      = llvm_builder->CreateOr(shift, result_elem_1);
            }
          }
          result_value = llvm_builder->CreateInsertElement(result_value, result_elem_0, (u64)0);
          result_value = llvm_builder->CreateInsertElement(result_value, result_elem_1, (u64)1);
          kto(opt_subgroup_size) { llvm_values_per_lane[k][res_id] = result_value; }
          break;
        }
        case spv::Op::OpGroupNonUniformBallotBitCount: {
          ASSERT_ALWAYS(WordCount == 6);
          u32 res_type_id = word1;
          u32 res_id      = word2;
          u32 scope_id    = word3;
          u32 scope       = get_global_const_i32(scope_id);
          u32 group_op    = word4;
          u32 value_id    = word5;
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          ASSERT_ALWAYS(result_type->isIntegerTy(32));
          llvm::SmallVector<llvm::Value *, 64> scan;
          scan.push_back(llvm_get_constant_i32(0));
          kto(opt_subgroup_size) {
            llvm::Value *val = llvm_values_per_lane[k][value_id];
            NOTNULL(val);
            ASSERT_ALWAYS(val->getType()->isVectorTy() && llvm_vec_num_elems(val->getType()) == 4 &&
                          llvm_vec_elem_type(val->getType())->isIntegerTy(32));
            llvm::Value *lane_mask_i32 =
                llvm_builder->CreateSExt(llvm_builder->CreateExtractElement(mask_register, (u64)k),
                                         llvm::Type::getInt32Ty(c));
            llvm::Value *popcnt_0 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (u64)0)});
            llvm::Value *popcnt_1 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (u64)1)});
            llvm::Value *popcnt_2 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (u64)2)});
            llvm::Value *popcnt_3 = llvm_builder->CreateIntrinsic(
                llvm::Intrinsic::ctpop, {llvm::IntegerType::getInt32Ty(c)},
                {llvm_builder->CreateExtractElement(val, (u64)3)});
            llvm::Value *popcnt = llvm_builder->CreateAdd(
                popcnt_0,
                llvm_builder->CreateAdd(popcnt_1, llvm_builder->CreateAdd(popcnt_2, popcnt_3)));
            popcnt = llvm_builder->CreateAnd(lane_mask_i32, popcnt);
            scan.push_back(llvm_builder->CreateAdd(popcnt, scan.back()));
          }
          switch ((spv::GroupOperation)group_op) {
          case spv::GroupOperation::GroupOperationExclusiveScan: {
            kto(opt_subgroup_size) { llvm_values_per_lane[k][res_id] = scan[k]; }
            break;
          }
          case spv::GroupOperation::GroupOperationInclusiveScan: {
            kto(opt_subgroup_size) { llvm_values_per_lane[k][res_id] = scan[k + 1]; }
            break;
          }
          case spv::GroupOperation::GroupOperationReduce: {
            kto(opt_subgroup_size) { llvm_values_per_lane[k][res_id] = scan.back(); }
            break;
          }
          default: UNIMPLEMENTED_(get_cstr((spv::GroupOperation)group_op));
          }
          break;
        }
        case spv::Op::OpGroupNonUniformElect: {
          ASSERT_ALWAYS(WordCount == 4);
          u32 res_type_id = word1;
          u32 res_id      = word2;
          u32 scope_id    = word3;
          u32 scope       = get_global_const_i32(scope_id);
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Value *result = llvm_builder->CreateVectorSplat(
              opt_subgroup_size,
              llvm::Constant::getIntegerValue(llvm::Type::getInt1Ty(c), llvm::APInt(1, 0)));
          ASSERT_ALWAYS(opt_subgroup_size <= 64);
          llvm::Value *cur_mask_packed = llvm_builder->CreateBitCast(
              mask_register, llvm::IntegerType::get(c, opt_subgroup_size));
          llvm::Value *cur_mask_i64 =
              llvm_builder->CreateZExt(cur_mask_packed, llvm::IntegerType::get(c, 64));
          llvm::Value *lsb           = llvm_builder->CreateCall(spv_lsb_i64, cur_mask_i64);
          llvm::Value *election_mask = llvm_builder->CreateShl(llvm_get_constant_i64(1), lsb);
          kto(opt_subgroup_size) {
            // lane_bit = (i1)((election_mask >> k) & 1)
            llvm::Value *lane_bit = llvm_builder->CreateTrunc(
                llvm_builder->CreateAnd(
                    llvm_builder->CreateLShr(election_mask, llvm_get_constant_i64(k)),
                    llvm_get_constant_i64(1)),
                llvm::IntegerType::getInt1Ty(c));
            llvm_values_per_lane[k][res_id] = lane_bit;
          }
          break;
        }
        case spv::Op::OpGroupNonUniformBroadcastFirst: {
          ASSERT_ALWAYS(WordCount == 5);
          u32 res_type_id = word1;
          u32 res_id      = word2;
          u32 scope_id    = word3;
          u32 value_id    = word4;
          u32 scope       = get_global_const_i32(scope_id);
          ASSERT_ALWAYS((spv::Scope)scope == spv::Scope::ScopeSubgroup);
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          llvm::ArrayType *result_arr = llvm::ArrayType::get(result_type, opt_subgroup_size);
          llvm::Value *    result     = llvm::UndefValue::get(result_arr);
          kto(opt_subgroup_size) {
            llvm::Value *lane_val = llvm_values_per_lane[k][value_id];
            result                = llvm_builder->CreateInsertValue(result, lane_val, {(u32)k});
          }
          llvm::Value *cur_mask_packed = llvm_builder->CreateBitCast(
              mask_register, llvm::IntegerType::get(c, opt_subgroup_size));
          llvm::Value *cur_mask_i64 =
              llvm_builder->CreateZExt(cur_mask_packed, llvm::IntegerType::get(c, 64));
          llvm::Value *lsb         = llvm_builder->CreateCall(spv_lsb_i64, cur_mask_i64);
          llvm::Value *stack_proxy = llvm_builder->CreateAlloca(result->getType());
          llvm_builder->CreateStore(result, stack_proxy);
          llvm::Value *gep = llvm_builder->CreateGEP(stack_proxy, {llvm_get_constant_i32(0), lsb});
          llvm::Value *broadcast = llvm_builder->CreateLoad(gep);
          kto(opt_subgroup_size) { llvm_values_per_lane[k][res_id] = broadcast; }
          break;
        }
        case spv::Op::OpAtomicISub:
        case spv::Op::OpAtomicOr:
        case spv::Op::OpAtomicIAdd: {
          ASSERT_ALWAYS(WordCount == 7);
          u32 res_type_id  = word1;
          u32 res_id       = word2;
          u32 pointer_id   = word3;
          u32 scope_id     = word4;
          u32 semantics_id = word5;
          u32 value_id     = word6;
          u32 scope        = get_global_const_i32(scope_id);
          u32 semantics    = get_global_const_i32(semantics_id);
          // TODO(aschrein): implement memory semantics
          //          spv::MemorySemanticsMask::MemorySemantics
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          ASSERT_ALWAYS(result_type->isIntegerTy() && result_type->getIntegerBitWidth() == 32);
          // Do they have different pointers?
          kto(opt_subgroup_size) {
            llvm::Value *val = llvm_values_per_lane[k][value_id];
            NOTNULL(val);
            ASSERT_ALWAYS(val->getType()->isIntegerTy() &&
                          val->getType()->getIntegerBitWidth() == 32);
            llvm::Value *ptr = llvm_values_per_lane[k][pointer_id];
            NOTNULL(ptr);
            ASSERT_ALWAYS(ptr->getType()->isPointerTy() &&
                          ptr->getType()->getPointerElementType()->isIntegerTy() &&
                          ptr->getType()->getPointerElementType()->getIntegerBitWidth() == 32);
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
          u32 res_type_id = word1;
          u32 res_id      = word2;
          // (var, parent_bb)
          llvm::SmallVector<std::pair<u32, u32>, 4> vars;
          ASSERT_ALWAYS((WordCount - 3) % 2 == 0);
          ito((WordCount - 3) / 2) { vars.push_back({pCode[3 + i * 2], pCode[3 + i * 2 + 1]}); }
          llvm::Type *result_type = llvm_types[res_type_id];
          NOTNULL(result_type);
          llvm::SmallVector<llvm::Value *, 64> dummy_values;
          kto(opt_subgroup_size) {
            //            llvm::PHINode *llvm_phi = llvm_builder->CreatePHI(result_type,
            //            (u32)vars.size()); ito(vars.size()) {
            //              llvm::Value *     value     = llvm_values_per_lane[k][vars[i].first];
            //              llvm::BasicBlock *parent_bb = llvm_labels[vars[i].second];
            //              NOTNULL(parent_bb);
            //              NOTNULL(value);
            //              llvm_phi->addIncoming(value, parent_bb);
            //            }
            //            deferred_per_lane_phi.push_back({llvm_phi});
            llvm::Value *dummy_value =
                new llvm::AllocaInst(result_type, 0, "phi_to_alloca", allocas_bb);

            // llvm_builder->CreateBitCast(llvm_builder->CreateCall(spv_dummy_call), result_type);
            dummy_values.push_back(dummy_value);
            llvm_values_per_lane[k][res_id] = llvm_builder->CreateLoad(dummy_value);
          }
          deferred_phis[cur_bb].push_back({.bb           = cur_bb,
                                           .vars         = vars,
                                           .dummy_values = dummy_values,
                                           .result_type  = result_type});
          break;
        }
#define SIMPLE_LLVM_OP(llvm_op)                                                                    \
  kto(opt_subgroup_size) {                                                                         \
    ASSERT_ALWAYS(llvm_values_per_lane[k][word2] == NULL);                                         \
    ASSERT_ALWAYS(llvm_values_per_lane[k][word3] != NULL);                                         \
    ASSERT_ALWAYS(llvm_values_per_lane[k][word4] != NULL);                                         \
    llvm_values_per_lane[k][word2] =                                                               \
        llvm_builder->llvm_op(llvm_values_per_lane[k][word3], llvm_values_per_lane[k][word4]);     \
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
        case spv::Op::OpSelect: {
          ASSERT_ALWAYS(WordCount == 6);
          u32 result_type_id = word1;
          u32 result_id      = word2;
          u32 cond_id        = word3;
          u32 a_id           = word4;
          u32 b_id           = word5;
          kto(opt_subgroup_size) {
            llvm::Value *cond = llvm_values_per_lane[k][cond_id];
            NOTNULL(cond);
            llvm::Value *a = llvm_values_per_lane[k][a_id];
            NOTNULL(a);
            llvm::Value *b = llvm_values_per_lane[k][b_id];
            NOTNULL(b);
            llvm_values_per_lane[k][result_id] = llvm_builder->CreateSelect(cond, a, b);
          }
          break;
        }
        case spv::Op::OpConvertUToF: {
          kto(opt_subgroup_size) {
            llvm::Type *dest_ty = llvm_types[word1];
            NOTNULL(dest_ty);
            llvm::Value *src_val = llvm_values_per_lane[k][word3];
            NOTNULL(src_val);
            llvm_values_per_lane[k][word2] = llvm_builder->CreateUIToFP(src_val, dest_ty);
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
          llvm_builder->CreateCall(kill, {state_ptr, llvm_get_constant_i32((u32)~0)});
          break;
        }
        case spv::Op::OpVectorTimesScalar: {
          kto(opt_subgroup_size) {
            llvm::Value *vector = llvm_values_per_lane[k][word3];
            llvm::Value *scalar = llvm_values_per_lane[k][word4];
            ASSERT_ALWAYS(vector != NULL && scalar != NULL);
            llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(vector->getType());
            ASSERT_ALWAYS(vtype != NULL);
            llvm::Value *splat = llvm_builder->CreateVectorSplat(llvm_vec_num_elems(vtype), scalar);
            llvm_values_per_lane[k][word2] = llvm_builder->CreateFMul(vector, splat);
          }
          break;
        }
        case spv::Op::OpCompositeExtract: {
          kto(opt_subgroup_size) {
            llvm::Value *src      = llvm_values_per_lane[k][word3];
            llvm::Type * src_type = src->getType();
            ASSERT_ALWAYS(WordCount > 4);
            u32          indices_count = WordCount - 4;
            llvm::Value *val           = src;
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
              llvm::Value *     undef = llvm::UndefValue::get(dst_type);
              llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(dst_type);
              ASSERT_ALWAYS(vtype != NULL);
              llvm::Value *final_val = undef;
              ito(llvm_vec_num_elems(vtype)) {
                llvm::Value *src = llvm_values_per_lane[k][pCode[3 + i]];
                ASSERT_ALWAYS(src != NULL);
                final_val = llvm_builder->CreateInsertElement(final_val, src, i);
              }
              llvm_values_per_lane[k][word2] = final_val;
            } else if (dst_type->isArrayTy()) {
              llvm::Value *    undef = llvm::UndefValue::get(dst_type);
              llvm::ArrayType *atype = llvm::dyn_cast<llvm::ArrayType>(dst_type);
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
            llvm::VectorType *vtype1 = llvm::dyn_cast<llvm::VectorType>(op1->getType());
            llvm::VectorType *vtype2 = llvm::dyn_cast<llvm::VectorType>(op2->getType());
            ASSERT_ALWAYS(vtype1 != NULL && vtype2 != NULL);
            std::vector<u32> indices;
            for (u16 i = 5; i < WordCount; i++) indices.push_back(pCode[i]);
            // In LLVM shufflevector must have both operands of the same type
            // In SPIRV operands may be of different types
            // Handle the different size case by creating one big vector and
            // construct the final vector by extracting elements from it
            if (llvm_vec_num_elems(vtype1) != llvm_vec_num_elems(vtype2)) {
              u32 total_width = llvm_vec_num_elems(vtype1) + llvm_vec_num_elems(vtype2);
              llvm::VectorType *new_vtype =
                  llvm::VectorType::get(llvm_vec_elem_type(vtype1), total_width);
              // Create a dummy super vector and appedn op1 and op2 elements
              // to it
              llvm::Value *prev = llvm::UndefValue::get(new_vtype);
              ito(llvm_vec_num_elems(vtype1)) {
                llvm::Value *extr = llvm_builder->CreateExtractElement(op1, i);
                prev              = llvm_builder->CreateInsertElement(prev, extr, i);
              }
              u32 offset = llvm_vec_num_elems(vtype1);
              ito(llvm_vec_num_elems(vtype2)) {
                llvm::Value *extr = llvm_builder->CreateExtractElement(op2, i);
                prev              = llvm_builder->CreateInsertElement(prev, extr, i + offset);
              }
              // Now we need to emit a chain of extact elements to make up the
              // result
              llvm::VectorType *res_type =
                  llvm::VectorType::get(llvm_vec_elem_type(vtype1), (u32)indices.size());
              llvm::Value *res = llvm::UndefValue::get(res_type);
              ito(indices.size()) {
                llvm::Value *elem = llvm_builder->CreateExtractElement(prev, indices[i]);
                res               = llvm_builder->CreateInsertElement(res, elem, i);
              }
              llvm_values_per_lane[k][word2] = res;
            } else {
              ASSERT_ALWAYS(llvm_builder && cur_bb != NULL);
              llvm_values_per_lane[k][word2] = llvm_builder->CreateShuffleVector(op1, op2, indices);
            }
          }
          break;
        }
        case spv::Op::OpDot: {
          kto(opt_subgroup_size) {
            u32          res_id  = word2;
            u32          op1_id  = word3;
            u32          op2_id  = word4;
            llvm::Value *op1_val = llvm_values_per_lane[k][op1_id];
            ASSERT_ALWAYS(op1_val != NULL);
            llvm::VectorType *op1_vtype = llvm::dyn_cast<llvm::VectorType>(op1_val->getType());
            ASSERT_ALWAYS(op1_vtype != NULL);
            llvm::Value *op2_val = llvm_values_per_lane[k][op2_id];
            ASSERT_ALWAYS(op2_val != NULL);
            llvm::VectorType *op2_vtype = llvm::dyn_cast<llvm::VectorType>(op2_val->getType());
            ASSERT_ALWAYS(op2_vtype != NULL);
            ASSERT_ALWAYS(llvm_vec_num_elems(op1_vtype) == llvm_vec_num_elems(op2_vtype));
            switch (llvm_vec_num_elems(op1_vtype)) {
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
            default: UNIMPLEMENTED;
            }
          }
          break;
        }
        case spv::Op::OpExtInst: {
          u32             result_type_id = word1;
          u32             result_id      = word2;
          u32             set_id         = word3;
          spv::GLSLstd450 inst           = (spv::GLSLstd450)word4;
          switch (inst) {
          case spv::GLSLstd450::GLSLstd450Normalize: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(arg != NULL);
              llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(arg->getType());
              ASSERT_ALWAYS(vtype != NULL);
              u32 width = llvm_vec_num_elems(vtype);
              switch (width) {
              case 2:
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(normalize_f2, {arg});
                break;
              case 3:
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(normalize_f3, {arg});
                break;
              case 4:
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(normalize_f4, {arg});
                break;
              default: UNIMPLEMENTED;
              }
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450Sqrt: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(arg != NULL);
              llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(arg->getType());
              if (vtype != NULL) {

                ASSERT_ALWAYS(vtype != NULL);
                u32          width = llvm_vec_num_elems(vtype);
                llvm::Value *prev  = arg;
                ito(width) {
                  llvm::Value *elem = llvm_builder->CreateExtractElement(arg, i);
                  llvm::Value *sqrt = llvm_builder->CreateCall(spv_sqrt, {elem});
                  prev              = llvm_builder->CreateInsertElement(prev, sqrt, i);
                }
                llvm_values_per_lane[k][result_id] = prev;
              } else {
                llvm::Type *type = arg->getType();
                ASSERT_ALWAYS(type->isFloatTy());
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_sqrt, {arg});
              }
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450Length: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[5]];
              ASSERT_ALWAYS(arg != NULL);
              llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(arg->getType());
              ASSERT_ALWAYS(vtype != NULL);
              u32 width = llvm_vec_num_elems(vtype);
              switch (width) {
              case 2:
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_length_f2, {arg});
                break;
              case 3:
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_length_f3, {arg});
                break;
              case 4:
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_length_f4, {arg});
                break;
              default: UNIMPLEMENTED;
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
              llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_cross, {op1, op2});
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
              llvm::Value *select                = llvm_builder->CreateSelect(cmp, op1, op2);
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
              llvm::Value *select                = llvm_builder->CreateSelect(cmp, op1, op2);
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
                llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(arg->getType());
                llvm::Value *     cmp   = llvm_builder->CreateFCmpOLT(
                    arg, llvm_builder->CreateVectorSplat(
                             llvm_vec_num_elems(vtype),
                             llvm::ConstantFP::get(llvm::Type::getFloatTy(c), 0.0)));
                llvm::Value *select = llvm_builder->CreateSelect(
                    cmp,
                    llvm_builder->CreateVectorSplat(
                        llvm_vec_num_elems(vtype),
                        llvm::ConstantFP::get(llvm::Type::getFloatTy(c), -1.0)),
                    llvm_builder->CreateVectorSplat(
                        llvm_vec_num_elems(vtype),
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
              llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_reflect, {I, N});
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
              llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_pow, {x, y});
            }
            break;
          }
          case spv::GLSLstd450::GLSLstd450FClamp: {
            ASSERT_ALWAYS(WordCount == 8);
            kto(opt_subgroup_size) {
              llvm::Value *x   = llvm_values_per_lane[k][word5];
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
          case spv::GLSLstd450::GLSLstd450FAbs: {
            ASSERT_ALWAYS(WordCount == 6);
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][word5];
              NOTNULL(arg);
              llvm::Value *result = NULL;
              if (arg->getType()->isVectorTy()) {
                llvm::VectorType *vtype = llvm::dyn_cast<llvm::VectorType>(arg->getType());
                ASSERT_ALWAYS(vtype != NULL);
                u32 width = llvm_vec_num_elems(vtype);
                switch (width) {
                case 2:
                  llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_fabs_f2, {arg});
                  break;
                case 3:
                  llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_fabs_f3, {arg});
                  break;
                case 4:
                  llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_fabs_f4, {arg});
                  break;
                default: UNIMPLEMENTED;
                }
              } else if (arg->getType()->isFloatTy()) {
                llvm_values_per_lane[k][result_id] = llvm_builder->CreateCall(spv_fabs_f1, {arg});

              } else {
                UNIMPLEMENTED;
              }
            }
            break;
          }

          default: UNIMPLEMENTED_(get_cstr(inst));
          }
#undef ARG
          break;
        }
        case spv::Op::OpReturnValue: {
          llvm::SmallVector<llvm::Value *, 64> currently_returned_values;
          ASSERT_ALWAYS(!contains(entries, item.first));
          u32 ret_value_id = word1;
          kto(opt_subgroup_size) {
            llvm::Value *ret_value = llvm_values_per_lane[k][ret_value_id];
            NOTNULL(ret_value);
            llvm_builder->CreateStore(ret_value, returned_values[k]);
            //            currently_returned_values.push_back(ret_value);
          }
          //          masked_store(currently_returned_values, returned_values);
          //          llvm::ReturnInst::Create(c, arr, cur_bb);
          terminators.insert(cur_bb);
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          cur_merge_id    = -1;
          cur_continue_id = -1;
          mask_register = NULL;
          break;
        }
        case spv::Op::OpReturn: {
          NOTNULL(cur_bb);
          if (contains(entries, item.first)) {
            for (auto &ds : deferred_stores) {
              llvm::Value *cast  = llvm_builder->CreateBitCast(ds.dst_ptr, ds.value_ptr->getType());
              llvm::Value *deref = llvm_builder->CreateLoad(ds.value_ptr);
              llvm_builder->CreateStore(deref, cast);
            }
          }
          //            llvm::ReturnInst::Create(c, NULL, cur_bb);
          terminators.insert(cur_bb);
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          cur_merge_id    = -1;
          cur_continue_id = -1;
          mask_register = NULL;
          break;
        }
        case spv::Op::OpFNegate: {
          ASSERT_ALWAYS(WordCount == 4);
          u32 result_type_id = word1;
          u32 result_id      = word2;
          u32 op_id          = word3;
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
            u32         res_type_id = word1;
            u32         res_id      = word2;
            u32         src_id      = word3;
            llvm::Type *res_type    = llvm_types[res_type_id];
            ASSERT_ALWAYS(res_type != NULL);
            llvm::Value *src = llvm_values_per_lane[k][src_id];
            ASSERT_ALWAYS(src != NULL);
            llvm_values_per_lane[k][res_id] = llvm_builder->CreateBitCast(src, res_type);
          }
          break;
        }
        case spv::Op::OpImageWrite: {
          // TODO: handle >4
          ASSERT_ALWAYS(WordCount == 4);
          u32 image_id = word1;
          u32 coord_id = word2;
          u32 texel_id = word3;
          kto(opt_subgroup_size) {
            llvm::Value *image = llvm_values_per_lane[k][image_id];
            ASSERT_ALWAYS(image != NULL);
            llvm::Value *coord = llvm_values_per_lane[k][coord_id];
            ASSERT_ALWAYS(coord != NULL);
            llvm::Value *texel = llvm_values_per_lane[k][texel_id];
            ASSERT_ALWAYS(texel != NULL);
            llvm_builder->CreateCall(lookup_image_op(texel->getType(), coord->getType(),
                                                     /*read =*/false),
                                     {image, coord, texel, get_lane_mask_bit(k)});
          }
          break;
        }
        case spv::Op::OpImageRead: {
          // TODO: handle >5
          ASSERT_ALWAYS(WordCount == 5);
          u32 res_type_id = word1;
          u32 res_id      = word2;
          u32 image_id    = word3;
          u32 coord_id    = word4;
          kto(opt_subgroup_size) {
            llvm::Type *res_type = llvm_types[res_type_id];
            ASSERT_ALWAYS(res_type != NULL);
            llvm::Value *image = llvm_values_per_lane[k][image_id];
            ASSERT_ALWAYS(image != NULL);
            llvm::Value *coord = llvm_values_per_lane[k][coord_id];
            ASSERT_ALWAYS(coord != NULL);
            llvm::Value *call = llvm_builder->CreateCall(
                lookup_image_op(res_type, coord->getType(), /*read =*/true), {image, coord});
            llvm_values_per_lane[k][res_id] = call;
          }
          break;
        }
        case spv::Op::OpFunctionCall: {
          u32          fun_id    = word3;
          u32          res_id    = word2;
          llvm::Value *fun_value = llvm_global_values[fun_id];
          NOTNULL(fun_value);
          llvm::Function *target_fun = llvm::dyn_cast<llvm::Function>(fun_value);
          NOTNULL(target_fun);
          llvm::SmallVector<llvm::Value *, 4> args;
          // Prepend state * and mask
          args.push_back(state_ptr);
          args.push_back(llvm_builder->CreateLoad(mask_register));
          llvm::Value *enabled_lanes = save_enabled_lanes_mask();
          u32          args_count    = (WordCount - 4) * opt_subgroup_size + 2;
          for (int i = 4; i < WordCount; i++) {
            kto(opt_subgroup_size) {
              llvm::Value *arg = llvm_values_per_lane[k][pCode[i]];
              NOTNULL(arg);
              args.push_back(arg);
            }
          }
          llvm::FunctionType *                target_fun_type = get_fn_ty(target_fun);
          llvm::SmallVector<llvm::Value *, 4> return_values;
          if (!is_void_fn(fun_id)) {
            // the rest is return values
            ASSERT_ALWAYS(target_fun_type->getNumParams() - args_count == opt_subgroup_size);

            for (u32 i = args_count; i < target_fun_type->getNumParams(); i++) {
              llvm::PointerType *ptr_ty =
                  llvm::dyn_cast<llvm::PointerType>(target_fun_type->getParamType(i));
              NOTNULL(ptr_ty);
              llvm::Value *alloca = llvm_builder->CreateAlloca(ptr_ty->getPointerElementType());
              return_values.push_back(alloca);
              args.push_back(alloca);
            }
          }
          llvm_builder->CreateCall(target_fun, args);
          if (!is_void_fn(fun_id)) {
            kto(opt_subgroup_size) {
              llvm_values_per_lane[k][res_id] = llvm_builder->CreateLoad(return_values[k]);
            }
          }
          restore_enabled_lanes_mask(enabled_lanes);
          break;
        }
        case spv::Op::OpMatrixTimesScalar: {
          ASSERT_ALWAYS(WordCount == 5);
          u32 result_type_id = word1;
          u32 result_id      = word2;
          u32 matrix_id      = word3;
          u32 scalar_id      = word4;
          kto(opt_subgroup_size) {
            llvm::Value *matrix = llvm_values_per_lane[k][matrix_id];
            llvm::Value *scalar = llvm_values_per_lane[k][scalar_id];
            NOTNULL(matrix);
            ASSERT_ALWAYS(matrix->getType()->isArrayTy() &&
                          matrix->getType()->getArrayElementType()->isVectorTy());
            llvm::Value *result   = llvm::UndefValue::get(matrix->getType());
            u32          row_size = llvm_vec_num_elems(matrix->getType()->getArrayElementType());
            ito(matrix->getType()->getArrayNumElements()) {
              llvm::Value *row = llvm_builder->CreateExtractValue(matrix, i);
              result           = llvm_builder->CreateInsertValue(
                  result,
                  llvm_builder->CreateFMul(row, llvm_builder->CreateVectorSplat(row_size, scalar)),
                  {0});
            }
            llvm_values_per_lane[k][result_id] = result;
          }
          break;
        }
        case spv::Op::OpVectorTimesMatrix: {
          ASSERT_ALWAYS(WordCount == 5);
          u32 result_type_id = word1;
          u32 result_id      = word2;
          u32 vector_id      = word3;
          u32 matrix_id      = word4;
          kto(opt_subgroup_size) {
            llvm::Value *matrix = llvm_values_per_lane[k][matrix_id];
            llvm::Value *vector = llvm_values_per_lane[k][vector_id];
            NOTNULL(matrix);
            ASSERT_ALWAYS(matrix->getType()->isArrayTy() &&
                          matrix->getType()->getArrayElementType()->isVectorTy());

            u32 vector_size = llvm_vec_num_elems(vector->getType());
            ASSERT_ALWAYS(vector_size == matrix->getType()->getArrayNumElements());
            u32 matrix_row_size = llvm_vec_num_elems(matrix->getType()->getArrayElementType());
            llvm::Type *result_type =
                llvm::VectorType::get(llvm_vec_elem_type(vector->getType()), matrix_row_size);
            llvm::Value *result   = llvm::UndefValue::get(result_type);
            llvm::Value *matrix_t = llvm_matrix_transpose(matrix, llvm_builder.get());
            llvm::SmallVector<llvm::Value *, 4> rows;
            jto(matrix_row_size) { rows.push_back(llvm_builder->CreateExtractValue(matrix_t, j)); }
            ito(matrix_row_size) {
              llvm::Value *dot_result = llvm_dot(vector, rows[i], llvm_builder.get());
              result                  = llvm_builder->CreateInsertElement(result, dot_result, i);
            }
            llvm_values_per_lane[k][result_id] = result;
          }
          break;
        }
        case spv::Op::OpMatrixTimesVector: {
          ASSERT_ALWAYS(WordCount == 5);
          u32 result_type_id = word1;
          u32 result_id      = word2;
          u32 matrix_id      = word3;
          u32 vector_id      = word4;
          kto(opt_subgroup_size) {
            llvm::Value *matrix = llvm_values_per_lane[k][matrix_id];
            llvm::Value *vector = llvm_values_per_lane[k][vector_id];
            NOTNULL(matrix);
            ASSERT_ALWAYS(matrix->getType()->isArrayTy() &&
                          matrix->getType()->getArrayElementType()->isVectorTy());

            u32 vector_size = llvm_vec_num_elems(vector->getType());

            u32 matrix_col_size = matrix->getType()->getArrayNumElements();
            ASSERT_ALWAYS(vector_size ==
                          llvm_vec_num_elems(matrix->getType()->getArrayElementType()));

            llvm::Type *result_type =
                llvm::VectorType::get(llvm_vec_elem_type(vector->getType()), matrix_col_size);
            llvm::Value *                       result = llvm::UndefValue::get(result_type);
            llvm::SmallVector<llvm::Value *, 4> rows;
            jto(matrix_col_size) { rows.push_back(llvm_builder->CreateExtractValue(matrix, j)); }
            ito(matrix_col_size) {
              llvm::Value *dot_result = llvm_dot(vector, rows[i], llvm_builder.get());
              result                  = llvm_builder->CreateInsertElement(result, dot_result, i);
            }
            llvm_values_per_lane[k][result_id] = result;
          }
          break;
        }
        case spv::Op::OpMatrixTimesMatrix: {
          ASSERT_ALWAYS(WordCount == 5);
          u32 result_type_id = word1;
          u32 result_id      = word2;
          u32 matrix_1_id    = word3;
          u32 matrix_2_id    = word4;
          uto(opt_subgroup_size) {
            // MUl N x K x M matrices
            llvm::Value *matrix_1 = llvm_values_per_lane[u][matrix_1_id];
            llvm::Value *matrix_2 = llvm_values_per_lane[u][matrix_2_id];
            NOTNULL(matrix_1);
            NOTNULL(matrix_2);
            ASSERT_ALWAYS(matrix_1->getType()->isArrayTy() &&
                          matrix_1->getType()->getArrayElementType()->isVectorTy());
            ASSERT_ALWAYS(matrix_2->getType()->isArrayTy() &&
                          matrix_2->getType()->getArrayElementType()->isVectorTy());
            llvm::Type *elem_type = llvm_vec_elem_type(matrix_1->getType()->getArrayElementType());
            u32         N         = (u32)matrix_1->getType()->getArrayNumElements();
            u32         K         = llvm_vec_num_elems(matrix_1->getType()->getArrayElementType());
            u32         K_1       = (u32)matrix_2->getType()->getArrayNumElements();
            ASSERT_ALWAYS(K == K_1);
            u32 M = llvm_vec_num_elems(matrix_2->getType()->getArrayElementType());

            llvm::Value *matrix_2_t = llvm_matrix_transpose(matrix_2, llvm_builder.get());
            llvm::SmallVector<llvm::Value *, 4> rows_1;
            llvm::SmallVector<llvm::Value *, 4> cols_2;
            jto(K) { cols_2.push_back(llvm_builder->CreateExtractValue(matrix_2_t, j)); }
            jto(K) { rows_1.push_back(llvm_builder->CreateExtractValue(matrix_1, j)); }
            llvm::Type * result_type = llvm::ArrayType::get(llvm::VectorType::get(elem_type, M), N);
            llvm::Value *result      = llvm::UndefValue::get(result_type);
            ito(N) {
              llvm::Value *result_row = llvm::UndefValue::get(result_type->getArrayElementType());
              jto(M) {
                llvm::Value *dot_result = llvm_dot(rows_1[i], cols_2[j], llvm_builder.get());
                result_row = llvm_builder->CreateInsertElement(result_row, dot_result, j);
              }
              result = llvm_builder->CreateInsertValue(result, result_row, i);
            }
            llvm_values_per_lane[u][result_id] = result;
          }
          break;
        }
        case spv::Op::OpImageSampleImplicitLod: {
          ASSERT_ALWAYS(WordCount == 5);
          u32         result_type_id   = word1;
          u32         result_id        = word2;
          u32         sampled_image_id = word3;
          u32         coordinate_id    = word4;
          u32         dim              = 0;
          llvm::Type *coord_elem_type  = NULL;
          ito(opt_subgroup_size) {
            llvm::Value *coord = llvm_values_per_lane[i][coordinate_id];
            NOTNULL(coord);
            if (dim == 0) {
              if (coord->getType()->isVectorTy()) {
                dim             = llvm_vec_num_elems(coord->getType());
                coord_elem_type = llvm_vec_elem_type(coord->getType());
              } else {
                dim             = 1;
                coord_elem_type = coord->getType();
              }
            } else {
              ASSERT_ALWAYS(dim == 1 || coord->getType()->isVectorTy() &&
                                            llvm_vec_num_elems(coord->getType()) == dim);
              ASSERT_ALWAYS(dim == 1 && coord->getType() == coord_elem_type ||
                            llvm_vec_elem_type(coord->getType()) == coord_elem_type);
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
            llvm::Type * dim_array_type = llvm::ArrayType::get(coord_elem_type, opt_subgroup_size);
            llvm::Value *dim_array      = llvm_builder->CreateAlloca(dim_array_type);
            llvm::Value *dim_ptr =
                llvm_builder->CreateBitCast(dim_array, llvm::PointerType::get(coord_elem_type, 0));
            jto(opt_subgroup_size) {
              llvm::Value *coord           = llvm_values_per_lane[j][coordinate_id];
              llvm::Value *coordinate_elem = llvm_builder->CreateExtractElement(coord, i);
              llvm::Value *gep = llvm_builder->CreateGEP(dim_ptr, llvm_get_constant_i32(j));
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
                llvm::ArrayType::get(llvm::VectorType::get(coord_elem_type, 2), opt_subgroup_size));
            llvm::Value *float2_arr_to_ptr = llvm_builder->CreateBitCast(
                dim_deriv_alloca,
                llvm::PointerType::get(llvm::VectorType::get(coord_elem_type, 2), 0));
            llvm_builder->CreateCall(
                get_derivatives, {state_ptr,
                                  llvm_builder->CreateBitCast(
                                      coordinates[i], llvm::PointerType::get(coord_elem_type, 0)),
                                  float2_arr_to_ptr});
            //	    llvm::Value *dim_deriv = llvm_builder->CreateAlloca();
            //            derivatives.push_back();
            derivatives.push_back(dim_deriv_alloca);
          }
          if (dim == 1) {
            UNIMPLEMENTED;
          } else if (dim == 2) {
            llvm::Value *derivatives_x = llvm_builder->CreateLoad(derivatives[0]);
            llvm::Value *derivatives_y = llvm_builder->CreateLoad(derivatives[1]);
            ito(opt_subgroup_size) {
              llvm::Value *dudxdy         = llvm_builder->CreateExtractValue(derivatives_x, {i});
              llvm::Value *dvdxdy         = llvm_builder->CreateExtractValue(derivatives_y, {i});
              llvm::Value *dudx           = llvm_builder->CreateExtractElement(dudxdy, (u64)0);
              llvm::Value *dvdx           = llvm_builder->CreateExtractElement(dvdxdy, (u64)0);
              llvm::Value *dudy           = llvm_builder->CreateExtractElement(dudxdy, (u64)1);
              llvm::Value *dvdy           = llvm_builder->CreateExtractElement(dvdxdy, (u64)1);
              llvm::Value *combined_image = llvm_values_per_lane[i][sampled_image_id];
              NOTNULL(combined_image);
              ASSERT_ALWAYS(combined_image->getType() == combined_image_t);
              llvm::Value *image_handle =
                  llvm_builder->CreateCall(get_combined_image, combined_image);
              llvm::Value *sampler_handle =
                  llvm_builder->CreateCall(get_combined_sampler, combined_image);
              llvm::Value *coord                 = llvm_values_per_lane[i][coordinate_id];
              llvm_values_per_lane[i][result_id] = llvm_builder->CreateCall(
                  spv_image_sample_2d_float4,
                  {image_handle, sampler_handle, llvm_builder->CreateExtractElement(coord, (u64)0),
                   llvm_builder->CreateExtractElement(coord, (u64)1), dudx, dudy, dvdx, dvdy});
            }
          } else {
            UNIMPLEMENTED;
          }
          break;
        }
        case spv::Op::OpConvertSToF: {
          ASSERT_ALWAYS(WordCount == 4);
          u32         result_type_id = word1;
          u32         result_id      = word2;
          u32         integer_val_id = word3;
          llvm::Type *result_type    = llvm_types[result_type_id];
          NOTNULL(result_type);
          ASSERT_ALWAYS(result_type->isFloatTy());
          kto(opt_subgroup_size) {
            llvm::Value *integer_val = llvm_values_per_lane[k][integer_val_id];
            ASSERT_ALWAYS(integer_val != NULL);
            ASSERT_ALWAYS(integer_val->getType()->isIntegerTy());
            llvm_values_per_lane[k][result_id] =
                llvm_builder->CreateSIToFP(integer_val, result_type);
          }
          break;
        }
        case spv::Op::OpUnreachable: {
          NOTNULL(cur_bb);
          new llvm::UnreachableInst(c, cur_bb);
          unreachables.insert(cur_bb);
          // Terminate current basic block
          cur_bb = NULL;
          llvm_builder.release();
          cur_merge_id    = -1;
          cur_continue_id = -1;
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
        case spv::Op::OpGroupMemberDecorate: break;
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
        case spv::Op::OpSubgroupAvcMceGetDefaultInterBaseMultiReferencePenaltyINTEL:
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
        case spv::Op::OpSubgroupAvcMceSetSingleReferenceInterlacedFieldPolarityINTEL:
        case spv::Op::OpSubgroupAvcMceSetDualReferenceInterlacedFieldPolaritiesINTEL:
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
        case spv::Op::OpSubgroupAvcMceGetInterReferenceInterlacedFieldPolaritiesINTEL:
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
        case spv::Op::OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeMotionVectorsINTEL:
        case spv::Op::OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeDistortionsINTEL:
        case spv::Op::OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeReferenceIdsINTEL:
        case spv::Op::OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeMotionVectorsINTEL:
        case spv::Op::OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeDistortionsINTEL:
        case spv::Op::OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeReferenceIdsINTEL:
        case spv::Op::OpSubgroupAvcImeGetBorderReachedINTEL:
        case spv::Op::OpSubgroupAvcImeGetTruncatedSearchIndicationINTEL:
        case spv::Op::OpSubgroupAvcImeGetUnidirectionalEarlySearchTerminationINTEL:
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
        default: {
          module->dump();
          UNIMPLEMENTED_(get_cstr(opcode));
        }
        }
      }
      // Remove unreachables
      {
        std::vector<llvm::BasicBlock *> new_bbs;
        for (llvm::BasicBlock *bb : bbs) {
          if (contains(unreachables, bb)) {
          } else {
            new_bbs.push_back(bb);
          }
        }
        bbs = new_bbs;
      }
      // @llvm/finish_function
      llvm::BasicBlock *global_terminator = llvm::BasicBlock::Create(c, "terminator", cur_fun);
      llvm::ReturnInst::Create(c, global_terminator);
      // stash for communication between basic blocks
      llvm::SmallDenseMap<llvm::Value *, llvm::Value *> shared_values_stash;

      //      // inst -> [inst that uses this instruction, op_id]
      //      std::map<llvm::Instruction *, llvm::SmallVector<std::pair<llvm::Instruction *,
      //      u32>, 4>>
      //          used_instructions;
      llvm::SmallDenseSet<llvm::Value *> shared_values;
      // A local cfg that is going to mirror the initial cfg
      llvm::SmallDenseMap<llvm::BasicBlock *, llvm::BasicBlock *, 16> local_cfg;
      struct WrappedBB {
        llvm::BasicBlock *                         bb;
        llvm::Function *                           fun;
        llvm::SmallDenseMap<llvm::Value *, u32, 8> in_args;
        llvm::SmallDenseMap<llvm::Value *, u32, 8> out_args;
      };
      std::map<llvm::BasicBlock *, WrappedBB> wrapped_bbs;
      //      for (llvm::BasicBlock *bb : bbs) {
      //        for (llvm::Instruction &inst : *bb) {
      //          for (llvm::Use &use : inst.uses()) {
      //            llvm::Instruction *user = llvm::dyn_cast<llvm::Instruction>(use.getUser());
      //            if (user != NULL) {
      //              used_instructions[&inst].push_back({user, use.getOperandNo()});
      //            }
      //          }
      //        }
      //      }
      //      auto create_masked_jump = [&](llvm::BasicBlock *dst, llvm::Value *mask) {
      //        llvm::BasicBlock *set_mask_bb = llvm::BasicBlock::Create(c, "set_mask", cur_fun);
      //        new llvm::StoreInst(mask, mask_registers[dst], set_mask_bb);
      //        llvm::BranchInst::Create(dst, set_mask_bb);
      //        return set_mask_bb;
      //      };
      // I'm trying to imitate basic blocks with parameters by having a bunch of alloced
      // slots to hold on the values
      auto call_bb = [&](llvm::BasicBlock *src, llvm::BasicBlock *dst) {
        ASSERT_ALWAYS(contains(wrapped_bbs, dst));
        WrappedBB &     wbb    = wrapped_bbs[dst];
        llvm::Function *bb_fun = wbb.fun;
        NOTNULL(bb_fun);
        llvm::SmallVector<llvm::Value *, 16> args;
        llvm::FunctionType *                 fun_ty = get_fn_ty(bb_fun);
        NOTNULL(fun_ty);
        args.resize(fun_ty->getNumParams());
        args[0] = state_ptr;
        args[1] = new llvm::LoadInst(mask_t, mask_registers[dst], "mask", src);
        for (auto &item : wbb.in_args) {
          if (contains(shared_values_stash, item.first)) {
            ASSERT_ALWAYS(contains(shared_values_stash, item.first));
            llvm::Value *      inst   = shared_values_stash[item.first];
            llvm::PointerType *ptr_ty = llvm::dyn_cast<llvm::PointerType>(inst->getType());
            NOTNULL(ptr_ty);
            args[item.second] =
                new llvm::LoadInst(ptr_ty->getPointerElementType(), inst,
                                   "param_" + bb_fun->getArg(item.second)->getName(), src);
          } else {
            args[item.second] = item.first;
          }
        }
        for (auto &item : wbb.out_args) {
          ASSERT_ALWAYS(contains(shared_values_stash, item.first));
          llvm::Value *inst = shared_values_stash[item.first];
          args[item.second] = inst;
        }
        llvm::CallInst::Create(bb_fun, args, "", src);
      };
      auto wrap_bb = [&](llvm::BasicBlock *bb) {
        WrappedBB wbb;
        wbb.bb = bb;
        bb->removeFromParent();
        llvm::SmallVector<llvm::Type *, 16> args;
        // Prepend state_t* to each function type
        args.push_back(state_t_ptr);
        // Prepend mask to each function type
        args.push_back(mask_t);
        // Prepend the work set ptr
        llvm::SmallDenseSet<llvm::Value *, 8> used_foreign_values;
        // Steps:
        // 1) promote foreign instructions to arguments
        // 2) insert pointers to local instructions to argumetns
        // 2) generate stores of local instructions to arguments

        // allocate arguments for foreign instructions
        for (llvm::Instruction &inst : *bb) {
          ito(inst.getNumOperands()) {
            llvm::Value *op = inst.getOperand(i);
            if (op == state_ptr || op == mask_registers[bb]) continue;
            llvm::Instruction *used_inst = llvm::dyn_cast<llvm::Instruction>(op);
            if (used_inst != NULL && used_inst->getParent() != bb &&
                !contains(used_foreign_values, used_inst)) {
              args.push_back(used_inst->getType());
              wbb.in_args[used_inst] = (u32)args.size() - 1;
              used_foreign_values.insert(used_inst);
              shared_values.insert(used_inst);
            }
            llvm::Argument *used_arg = llvm::dyn_cast<llvm::Argument>(op);
            if (used_arg != NULL && !contains(used_foreign_values, used_arg)) {
              args.push_back(used_arg->getType());
              wbb.in_args[used_arg] = (u32)args.size() - 1;
              used_foreign_values.insert(used_arg);
              shared_values.insert(used_arg);
            }
          }
        }

        u32 out_args_begin = args.size();
        // allocate arguments for local instructions that are used outside
        for (llvm::Instruction &inst : *bb) {
          // if has users
          bool has_foreign_users = contains(conditions, &inst);
          for (llvm::User *user : inst.users()) {
            llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(user);
            if (inst != NULL && inst->getParent() != bb) {
              has_foreign_users = true;
              break;
            }
          }
          if (has_foreign_users) {
            args.push_back(llvm::PointerType::get(inst.getType(), 0));
            wbb.out_args[&inst] = (u32)args.size() - 1;
            shared_values.insert(&inst);
          }
        }

        llvm::BasicBlock *mirror_bb = llvm::BasicBlock::Create(c, "pseudo_" + bb->getName());
        mirror_bb->insertInto(cur_fun);
        char tmp_buf[0x100];
        snprintf(tmp_buf, sizeof(tmp_buf), "deinlined_%s", bb->getName().str().c_str());
        llvm::Function *fun =
            llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(c), args, false),
                                   llvm::GlobalValue::InternalLinkage, tmp_buf, module.get());
        bb->insertInto(fun);
        llvm::Instruction *this_mask = NULL;
        if (bb->begin() == bb->end()) {
          this_mask                     = new llvm::AllocaInst(mask_t, 0, "mask_alloca", bb);
          llvm::Instruction *store_mask = new llvm::StoreInst(fun->getArg(1), this_mask, bb);
        } else {
          this_mask = new llvm::AllocaInst(mask_t, 0, "mask_alloca", &bb->front());
          llvm::Instruction *store_mask =
              new llvm::StoreInst(fun->getArg(1), this_mask, this_mask->getNextNode());
        }

        //        store_mask->insertBefore(&bb->front());
        //        this_mask->insertBefore(store_mask);
        llvm::UnreachableInst *unreach = NULL;
        // Replace state ptr
        for (llvm::Instruction &inst : *bb) {
          ito(inst.getNumOperands()) {
            if (inst.getOperand(i) == state_ptr) {
              inst.setOperand(i, fun->getArg(0));
            } else if (inst.getOperand(i) == mask_registers[bb]) {
              inst.setOperand(i, this_mask);
            }
          }
          if (llvm::isa<llvm::UnreachableInst>(&inst)) {
            ASSERT_ALWAYS(unreach == NULL);
            unreach = llvm::dyn_cast<llvm::UnreachableInst>(&inst);
          }
        }
        // No unreachable bbs
        ASSERT_ALWAYS(unreach == NULL);
        // Store locals to the arguments
        for (auto &item : wbb.out_args) {
          new llvm::StoreInst(item.first, fun->getArg(item.second), bb);
        }
        for (u32 i = out_args_begin; i < fun->arg_size(); ++i) {
          fun->getArg(i)->setName("out");
        }
        for (auto &item : wbb.in_args) {
          llvm::Value *arg              = fun->getArg(item.second);
          llvm::Value *value_to_replace = item.first;
          ASSERT_ALWAYS(!contains(wbb.out_args, item.first));

          arg->setName("in");

          for (llvm::Instruction &inst : *bb) {
            ito(inst.getNumOperands()) {
              llvm::Value *op = inst.getOperand(i);
              if (op == value_to_replace) {
                inst.setOperand(i, arg);
              }
            }
          }
        }
        if (unreach != NULL) {
          unreach->removeFromParent();
          unreach->deleteValue();
        }
        llvm::ReturnInst::Create(c, NULL, bb);
        wbb.fun         = fun;
        wrapped_bbs[bb] = wbb;
        local_cfg[bb]   = mirror_bb;

        return fun;
      };

      // Replace phi nodes with regular load/stores
      for (auto &item : deferred_phis) {
        llvm::BasicBlock *bb = item.first;
        for (auto &j : item.second) {

          ito(j.vars.size()) {
            llvm::SmallVector<llvm::Value *, 64> values;
            llvm::SmallVector<llvm::Value *, 64> addresses;
            llvm::BasicBlock *                   parent_bb = llvm_labels[j.vars[i].second];
            kto(opt_subgroup_size) {

              llvm::Value *value = llvm_values_per_lane[k][j.vars[i].first];

              values.push_back(value);
              addresses.push_back(j.dummy_values[k]);
              //              new llvm::StoreInst(value, j.dummy_values[k], parent_bb);
            }
            masked_store(values, addresses, parent_bb);
          }
        }
      }

      // Wrap each basic block in a function
      // This is going to build a table of export/import(shared) values fro bbs
      for (llvm::BasicBlock *bb : bbs) {
        wrap_bb(bb);
      }

      // Allocate a storage for each shared variable
      for (llvm::Value *item : shared_values) {
        llvm::Value *val = item;
        shared_values_stash[val] =
            new llvm::AllocaInst(val->getType(), 0, "proxy_" + val->getName(), allocas_bb);
        if (llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(val)) {
          if (inst->getParent() == allocas_bb) {
            new llvm::StoreInst(val, shared_values_stash[val], allocas_bb);
          }
        } else if (llvm::Argument *arg = llvm::dyn_cast<llvm::Argument>(val)) {
          new llvm::StoreInst(val, shared_values_stash[val], allocas_bb);
        } else {
          TRAP;
        }
      }

      // genenrates load stores for import/export variables at call site
      for (llvm::BasicBlock *bb : bbs) {
        call_bb(local_cfg[bb], bb);
      }

      // We want to restrict supported configurations to just structured CFG
      // for example:
      //
      //    |  |
      //    A  B
      //   / \/ \  we're gonna have troubles merging at D
      //  C  D  E  so we want each predecessor to be dominated by only one branch block
      //  |  |  |
      //
      // Structured CFG basic patterns:
      //
      //   A     A    A        |
      //   |    /|   / \     <-A<-+
      //   B   B |  B   C      |  |
      //        \|   \ /       B--+
      //         C    D
      // Basically those are Single entry Single exit building blocks
      //
      // node per bb
      // keeps information needed to handle masks
      struct CFG_Node {
        u32                                        id   = 0;
        llvm::BasicBlock *                         prev = NULL;
        llvm::BasicBlock *                         next = NULL;
        llvm::SmallDenseSet<llvm::BasicBlock *, 2> in_bbs;
        llvm::SmallDenseSet<llvm::BasicBlock *, 2> out_bbs;
        llvm::BasicBlock *                         merge = NULL;
        llvm::BasicBlock *                         cont  = NULL;
      };
      std::map<llvm::BasicBlock *, CFG_Node> bb_cfg;
      local_cfg[allocas_bb]        = allocas_bb;
      local_cfg[global_terminator] = global_terminator;
      // dst -> (src -> mask)
      std::map<llvm::BasicBlock *, std::map<llvm::BasicBlock *, llvm::Value *>> cond_edges;
      // Build cfg
      // We don't have anything but branches to govern our CFG
      for (BranchCond &cb : deferred_branches) {
        llvm::BasicBlock *bb = cb.bb;
        ASSERT_ALWAYS(bb != NULL);
        llvm::BasicBlock *dst_true = llvm_labels[cb.true_id];
        NOTNULL(dst_true);
        llvm::BasicBlock *dst_false    = NULL;
        llvm::BasicBlock *dst_merge    = NULL;
        llvm::BasicBlock *dst_continue = NULL;
        if (cb.false_id != 0) {
          dst_false = llvm_labels[cb.false_id];
          bb_cfg[bb].out_bbs.insert(dst_false);
          bb_cfg[dst_false].in_bbs.insert(bb);
        }
        if (cb.merge_id != 0) {
          dst_merge        = llvm_labels[cb.merge_id];
          bb_cfg[bb].merge = dst_merge;
        }
        if (cb.continue_id != 0) {
          dst_continue    = llvm_labels[cb.continue_id];
          bb_cfg[bb].cont = dst_continue;
        }
        bb_cfg[bb].out_bbs.insert(dst_true);
        bb_cfg[dst_true].in_bbs.insert(bb);
      }

      // Remove unreachables
      {
        for (llvm::BasicBlock *bb : unreachables) {
          // Make sure it's really unreachable
          // TODO: Remove branhces to unreachable or emit llvm analog
          // they might be actually important to convey constraints
          ASSERT_ALWAYS(bb_cfg[bb].in_bbs.size() == 0);
          bb_cfg.erase(bb);
          bb->removeFromParent();
          bb->deleteValue();
        }
      }

      // reachable relation query. uses a cache to amortize stuff
      // we need it to detect loops
      //
      // bb reachable from [...]
      std::set<std::pair<llvm::BasicBlock *, llvm::BasicBlock *>> reachable_cache;
      // true if b is reachable from a
      std::function<bool(llvm::BasicBlock * a, llvm::BasicBlock * b)> reachable =
          [&](llvm::BasicBlock *a, llvm::BasicBlock *b) {
            if (a == b) return true;
            if (contains(reachable_cache, std::pair<llvm::BasicBlock *, llvm::BasicBlock *>{b, a}))
              return true;
            std::set<llvm::BasicBlock *>   visited;
            std::deque<llvm::BasicBlock *> to_visit;
            to_visit.push_back(b);
            while (to_visit.size() != 0) {
              llvm::BasicBlock *cur = to_visit.front();
              to_visit.pop_front();
              if (cur == a) return true;
              if (contains(visited, cur)) continue;
              visited.insert(cur);
              for (llvm::BasicBlock *in : bb_cfg[cur].in_bbs) {
                reachable_cache.insert({b, in});
                if (in == a) {
                  return true;
                }
                // breadth-first
                to_visit.push_back(in);
              }
            }
            return false;
          };
      // bb dominated by [...]
      std::map<llvm::BasicBlock *, std::set<llvm::BasicBlock *>> dom_cache;
      std::map<llvm::BasicBlock *, std::set<llvm::BasicBlock *>> postdom_cache;
      // Naive O(N^2)(at least. O(logN * N^2) in reality) algorithm
      // TODO: Lengauer-Tarjan
      {
        for (llvm::BasicBlock *bb : bbs) dom_cache[bb] = {bb};
        for (llvm::BasicBlock *bb : bbs) {
          if (bb == entry_bb) continue;
          std::set<llvm::BasicBlock *>   visited;
          std::deque<llvm::BasicBlock *> to_visit;
          to_visit.push_back(entry_bb);
          while (!to_visit.empty()) {
            llvm::BasicBlock *cur = to_visit.front();
            to_visit.pop_front();
            if (contains(visited, cur)) continue;
            visited.insert(cur);
            for (llvm::BasicBlock *out : bb_cfg[cur].out_bbs) {
              if (out != bb) to_visit.push_front(out);
            }
          }
          for (llvm::BasicBlock *i : bbs)
            if (!contains(visited, i)) dom_cache[i].insert(bb);
        }
        if (0) {
          for (auto &item : dom_cache) {
            fprintf(stdout, "%s dominated by\n", item.first->getName().str().c_str());
            for (auto &dom : item.second) {
              fprintf(stdout, "  %s\n", dom->getName().str().c_str());
            }
          }
          fflush(stdout);
        }
      }
      // Post dominator tree
      // Naive N^2 solution
      // assume every exit is post dominated by a virtual single exit
      {
        for (llvm::BasicBlock *bb : bbs) {
          for (llvm::BasicBlock *bb2 : bbs) {
            postdom_cache[bb].insert(bb2);
          }
        }
        bool change = true;
        while (change) {
          change = false;
          for (llvm::BasicBlock *bb : bbs) {
            std::set<llvm::BasicBlock *> intersection;
            for (llvm::BasicBlock *cur : bb_cfg[bb].out_bbs) {
              std::set<llvm::BasicBlock *> p_doms = postdom_cache[cur];
              if (intersection.empty())
                intersection = p_doms;
              else {
                intersection = get_intersection(intersection, p_doms);
              }
            }
            intersection.insert(bb);
            bool this_change = !sets_equal(intersection, postdom_cache[bb]);
            if (this_change) {
              postdom_cache[bb] = intersection;
              change            = true;
            }
          }
        }
        // @Debug
        if (0) {
          for (auto &item : postdom_cache) {
            fprintf(stdout, "%s postdominated by\n", item.first->getName().str().c_str());
            for (auto &dom : item.second) {
              fprintf(stdout, "  %s\n", dom->getName().str().c_str());
            }
          }
          fflush(stdout);
        }
      }
      // true if b is dominated by a
      std::function<bool(llvm::BasicBlock * a, llvm::BasicBlock * b)> dominates =
          [&](llvm::BasicBlock *a, llvm::BasicBlock *b) {
            ASSERT_ALWAYS(contains(dom_cache, b));
            return contains(dom_cache[b], a);
          };
      // true if b is postdominated by a
      std::function<bool(llvm::BasicBlock * a, llvm::BasicBlock * b)> postdominates =
          [&](llvm::BasicBlock *a, llvm::BasicBlock *b) {
            ASSERT_ALWAYS(contains(postdom_cache, b));
            return contains(postdom_cache[b], a);
          };
      auto get_closest_branch = [&](llvm::BasicBlock *bb) {
        std::set<llvm::BasicBlock *> doms   = dom_cache[bb];
        u32                          max_id = 0;
        llvm::BasicBlock *           max_bb = NULL;
        for (llvm::BasicBlock *dom : doms) {
          if (contains(cond_edges, dom)) {
            CFG_Node &node = bb_cfg[dom];
            if (node.id > max_id) {
              max_id = node.id;
              max_bb = dom;
            }
          }
        }
        return max_bb;
      };
      // strict
      auto get_closest_postdom = [&](llvm::BasicBlock *bb) -> llvm::BasicBlock * {
        std::set<llvm::BasicBlock *>   postdoms = postdom_cache[bb];
        std::set<llvm::BasicBlock *>   visited;
        std::deque<llvm::BasicBlock *> to_visit;
        to_visit.push_back(bb);
        while (!to_visit.empty()) {
          llvm::BasicBlock *cur = to_visit.front();
          to_visit.pop_front();
          if (cur != bb && contains(postdoms, cur)) return cur;
          if (contains(visited, cur)) continue;
          visited.insert(cur);
          for (llvm::BasicBlock *out : bb_cfg[cur].out_bbs) {
            to_visit.push_back(out);
          }
        }
        return NULL;
      };
      // clang-format off
      //   __          ___ _    _                _ _
      //   \ \        / (_) |  (_)              | (_)
      //    \ \  /\  / / _| | ___ _ __   ___  __| |_  __ _
      //     \ \/  \/ / | | |/ / | '_ \ / _ \/ _` | |/ _` |
      //      \  /\  /  | |   <| | |_) |  __/ (_| | | (_| |
      //       \/  \/   |_|_|\_\_| .__/ \___|\__,_|_|\__,_|
      //                         | |
      //                         |_|
      //
      // Some definitions for quick reference:
      // +----------------------------------------------------------------------------------------------------
      // | -- A back edge is a CFG edge whose target dominates its source.
      // |
      // | -- A natural loop for back edge t → h is a subgraph containing t and h,
      // | and all nodes from which t can be reached without passing through h.
      // |
      // | -- A loop header (sometimes called the entry point of the loop) is a dominator that is the
      // | target of a loop-forming back edge. The loop header dominates all blocks in the loop body.
      // | A block may be a loop header for more than one loop. A loop may have multiple entry points,
      // | in which case it has no "loop header".
      // |
      // | -- The loop for a header h is the union of all natural loops for back edges whose target is h.
      // |
      // | -- A subgraph of a graph is strongly connected if there is a path in the subgraph from every
      // | node to every other node.
      // |
      // | -- Every loop is a strongly connected subgraph.
      // |
      // | -- A CFG is reducible if every strongly connected subgraph contains a unique node (the header)
      // | that dominates all nodes in the subgraph.
      // |
      // | -- A reducible CFG is one with edges that can be partitioned into two disjoint sets:
      // | forward edges, and back edges, such that:
      // | * Forward edges form a directed acyclic graph with all nodes reachable from the entry node.
      // | * For all back edges (A, B), node B dominates node A.
      // +----------------------------------------------------------------------------------------------------
      //
      // Now we gotta linearize CFG
      // it's easy just sort bbs in topological order so that
      // every bb comes before all bbs that are reachable from it
      //
      //       |
      //       A
      //      / \   ===>  A -> B -> C -> D
      //     B   C
      //      \ /
      //       D
      //       |
      //
      // Except there are loops. we allow only reducible loops
      // the ones with only one entry point
      //
      //   +--A <-+
      //   |  |   |
      //   |  C---+
      //   |  |
      //   B  D      where C could be a nested CFG with loops
      //             (A, C) is a strognly connected component here - a loop
      //
      // -> D -> A -> C -> B-->
      //    ^    ^____|    |   one way of loop linearization
      //    |              |
      //    |______________|
      //
      // clang-format on

      std::set<std::pair<llvm::BasicBlock *, llvm::BasicBlock *>> back_edges;
      std::set<llvm::BasicBlock *>                                loop_headers;
      auto is_back_edge = [&](llvm::BasicBlock *from, llvm::BasicBlock *to) {
        return contains(back_edges, std::pair<llvm::BasicBlock *, llvm::BasicBlock *>{from, to});
      };
      {
        for (llvm::BasicBlock *cur : bbs) {
          for (llvm::BasicBlock *out : bb_cfg[cur].out_bbs) {
            if (dominates(out, cur)) {
              back_edges.insert({cur, out});
              loop_headers.insert(out);
            }
          }
        }
      }

      // The reason why we can't just sort based on breadth first is this:
      //
      //    A
      //   / \
      //  B  /  in breadth first C could be placed before B
      //  \ /
      //   C
      //        so we actually need topological sort (loop aware)
      //          in loops the jump always goes to the block that is
      //        earlier in the path from entry to the jumping node (back edge)
      //
      // sort cfg
      {
        std::vector<llvm::BasicBlock *> sorted_bbs;
        std::set<llvm::BasicBlock *>    sorted_bbs_set;
        // a queue of nodes which weren't yet sorted
        std::deque<llvm::BasicBlock *> to_visit;
        {
          sorted_bbs.push_back(allocas_bb);
          sorted_bbs_set.insert(allocas_bb);
          to_visit.push_back(entry_bb);
        }
        // loop aware topo-sort
        while (!to_visit.empty()) {
          llvm::BasicBlock *bb = to_visit.front();
          to_visit.pop_front();
          if (contains(sorted_bbs_set, bb)) continue;
          // ready when all dependencies are ready or there are none
          bool ready_to_insert = true;
          for (llvm::BasicBlock *in : bb_cfg[bb].in_bbs) {
            if (!contains(sorted_bbs_set, in) && !is_back_edge(in, bb)) {
              ready_to_insert = false;
              break;
            }
          }
          if (!ready_to_insert) {
            // put at the end of queue
            to_visit.push_back(bb);
            continue;
          }
          // For loops we want to emit the blocks from the same
          // strongly connected component first
          std::set<llvm::BasicBlock *> reachables;
          std::set<llvm::BasicBlock *> unreachables;
          for (llvm::BasicBlock *out : bb_cfg[bb].out_bbs) {
            if (reachable(out, bb))
              reachables.insert(out);
            else
              unreachables.insert(out);
          }
          // depth-first to make sure successors of a branch are part of the original CFG
          if (reachables.size() != 0) {
            for (llvm::BasicBlock *out : unreachables) {
              to_visit.push_back(out);
            }
            for (llvm::BasicBlock *out : reachables) {
              to_visit.push_front(out);
            }
          } else {
            for (llvm::BasicBlock *out : unreachables) {
              to_visit.push_front(out);
            }
          }

          sorted_bbs_set.insert(bb);
          sorted_bbs.push_back(bb);
        }
        ito(sorted_bbs.size() - 1) {
          bb_cfg[sorted_bbs[i]].next     = sorted_bbs[i + 1];
          bb_cfg[sorted_bbs[i + 1]].prev = sorted_bbs[i];
        }
        bb_cfg[sorted_bbs.back()].next = global_terminator;
        ito(sorted_bbs.size()) { bb_cfg[sorted_bbs[i]].id = i; }
      }

      // Now to the mask handling
      // +-----------------------------------------------------------------
      // | We have basically these edges:
      // +-----------------------------------------------------------------
      // | 1) A -> B unconditionally, just pass-through. no need for masks
      // | 2) A -> B is a conditional branch
      // |    I)  A comes after B in linearized CFG
      // |    II) A comes after some other node in linearized CFG
      // | 3) A -> B is a back jump
      // | 4) A -> nil - return, need to disable the lane for this function
      // |
      // +-----------------------------------------------------------------
      // | A node could be either one of these or some of these:
      // +-----------------------------------------------------------------
      // | 1) Entry point
      // | 2) Loop header - could also be a branched node
      // | 3) Pass-through
      // | 4) Conditionally branched(2 edges only) - no switch case for now
      // | 5) Terminator - a return statement at the end
      // | 6) Unreachable - not handled
      // |
      // +-----------------------------------------------------------------
      // | We have at least two masks: one for conditional execution, one
      // | to signify disabled(returned) lanes. Also one for fragment shader
      // | for discards.
      // | Every bit of observable behavior should be masked. naive approach
      // | is to mask every store. keeping the loads unmasked. of course this
      // | leads to UB when addresses are invalid but we don't care at that
      // | stage yet.
      // |
      // +-----------------------------------------------------------------

      {
        llvm_builder.reset(new LLVM_IR_Builder_t(local_cfg[allocas_bb], llvm::NoFolder()));
        defer(llvm_builder.release());
        llvm_builder->CreateStore(
            llvm_builder->CreateLoad(mask_registers[allocas_bb], "initial_mask"),
            mask_registers[entry_bb]);
        llvm::BranchInst::Create(local_cfg[entry_bb], allocas_bb);
      }
      for (BranchCond &cb : deferred_branches) {
        llvm::BasicBlock *bb = cb.bb;
        ASSERT_ALWAYS(bb != NULL);
        llvm::BasicBlock *dst_true = llvm_labels[cb.true_id];
        NOTNULL(dst_true);
        CFG_Node &        node      = bb_cfg[bb];
        llvm::BasicBlock *dst_false = NULL;
        if (cb.false_id != 0) {
          dst_false = llvm_labels[cb.false_id];
        }
        llvm_builder.reset(new LLVM_IR_Builder_t(local_cfg[bb], llvm::NoFolder()));
        defer(llvm_builder.release());

        if (cb.cond_id != 0) {
          llvm::Value *i1_vec = llvm::UndefValue::get(
              llvm::VectorType::get(llvm::Type::getInt1Ty(c), opt_subgroup_size));
          kto(opt_subgroup_size) {
            llvm::Instruction *src_cond =
                llvm::dyn_cast<llvm::Instruction>(llvm_values_per_lane[k][cb.cond_id]);
            NOTNULL(src_cond);
            llvm::Value *cond = llvm_builder->CreateLoad(shared_values_stash[src_cond]);
            NOTNULL(cond);
            ASSERT_ALWAYS(cond->getType()->isIntegerTy());
            i1_vec = llvm_builder->CreateInsertElement(i1_vec, cond, k);
          }
          llvm::Value *cond_mask = i1_vec_to_mask(i1_vec);
          llvm::Value *mask      = llvm_builder->CreateLoad(mask_registers[bb], "mask");
          llvm::Value *true_mask = llvm_builder->CreateAnd(mask, cond_mask, "true_mask");
          llvm::Value *false_mask =
              llvm_builder->CreateAnd(mask, llvm_builder->CreateNot(cond_mask), "false_mask");
          NOTNULL(dst_false);

          {
            llvm::Value *true_target_mask =
                llvm_builder->CreateLoad(mask_registers[dst_true], "true_target_mask");
            llvm_builder->CreateStore(llvm_builder->CreateOr(true_mask, true_target_mask),
                                      mask_registers[dst_true]);
          }
          {
            llvm::Value *false_target_mask =
                llvm_builder->CreateLoad(mask_registers[dst_false], "false_target_mask");
            llvm_builder->CreateStore(llvm_builder->CreateOr(false_mask, false_target_mask),
                                      mask_registers[dst_false]);
          }
          ASSERT_ALWAYS(node.out_bbs.size() == 2);
          // the way we sort enforces this ordering
          ASSERT_ALWAYS(node.next == dst_true || node.next == dst_false);
          ASSERT_ALWAYS(!is_back_edge(bb, dst_true) && !is_back_edge(bb, dst_false));
          llvm::Value *     next_mask   = true_mask;
          llvm::BasicBlock *other_block = dst_false;
          if (node.next == dst_false) {
            next_mask   = false_mask;
            other_block = dst_true;
          }
          // reset the mask register at the end of bb
          new llvm::StoreInst(llvm_get_constant_i64(0), mask_registers[bb], local_cfg[bb]);
          llvm::Value *all_false =
              new llvm::ICmpInst(*local_cfg[bb], llvm::ICmpInst::Predicate::ICMP_EQ, next_mask,
                                 llvm_get_constant_i64(0));
          llvm::BranchInst::Create(local_cfg[other_block], local_cfg[node.next], all_false,
                                   local_cfg[bb]);

        } else {

          llvm::Value *true_target_mask =
              llvm_builder->CreateLoad(mask_registers[dst_true], "true_target_mask");
          llvm::Value *mask = llvm_builder->CreateLoad(mask_registers[bb], "mask");
          llvm_builder->CreateStore(llvm_builder->CreateOr(mask, true_target_mask),
                                    mask_registers[dst_true]);
          // reset the mask register at the end of bb
          new llvm::StoreInst(llvm_get_constant_i64(0), mask_registers[bb], local_cfg[bb]);
          if (is_back_edge(bb, dst_true)) {
            llvm::BranchInst::Create(local_cfg[dst_true], local_cfg[bb]);
          } else {
            // Fall-through
            llvm::BranchInst::Create(local_cfg[node.next], local_cfg[bb]);
          }
        }
      }
      // @Debug
      if (0) {
        for (llvm::BasicBlock *bb : bbs) {
          if (bb_cfg[bb].out_bbs.size() < 2) continue;
          fprintf(stdout, "%s postdominated by %s\n", bb->getName().str().c_str(),
                  get_closest_postdom(bb)->getName().str().c_str());
        }
        fflush(stdout);
      }
      for (llvm::BasicBlock *item : terminators) {
        CFG_Node &node = bb_cfg[item];
        NOTNULL(node.next);
        if (node.next == global_terminator) {
          llvm::BranchInst::Create(local_cfg[node.next], local_cfg[item]);
        } else {
          llvm::Value *all_lanes_are_off =
              llvm::CallInst::Create(spv_disable_lanes,
                                     {state_ptr, new llvm::LoadInst(mask_t, mask_registers[item],
                                                                    "old_mask", local_cfg[item])},
                                     "return", local_cfg[item]);
          llvm::BranchInst::Create(global_terminator, local_cfg[node.next], all_lanes_are_off,
                                   local_cfg[item]);
        }
      }
      auto dump_cfg = [&]() {
        static std::map<llvm::BasicBlock *, u32> bb_to_id;
        {
          static u32 id      = 1;
          bb_to_id[entry_bb] = id++;
          for (auto &bb : bbs) {
            bb_to_id[bb] = id++;
          }
          bb_to_id[global_terminator] = id++;
        }
        static FILE *dotgraph = NULL;
        static defer({
          fprintf(dotgraph, "}\n");
          fflush(dotgraph);
          fclose(dotgraph);
          dotgraph = NULL;
        });
        if (dotgraph == NULL) {
          dotgraph = fopen("cfg.dot", "wb");
          fprintf(dotgraph, "digraph {\n");
        }
        fprintf(dotgraph, "node [shape=record];\n");
        // 1348 -> 1350 [style=dashed];
        // 1348 -> 1351 [style=dotted];
        fprintf(dotgraph, "%i [label = \"terminator\", shape = record];\n",
                bb_to_id[global_terminator]);
        for (llvm::BasicBlock *bb : bbs) {
          fprintf(dotgraph, "%i [style=filled, label = \"%s\", shape = %s, fillcolor = %s];\n",
                  bb_to_id[bb], bb->getName().str().c_str(),
                  contains(terminators, bb)
                      ? "invtriangle"
                      : bb_cfg[bb].in_bbs.size() == 0
                            ? "record"
                            : bb_cfg[bb].out_bbs.size() > 1 ? "triangle" : "circle",
                  //                  contains(looped_bbs, bb)
                  //                      ? (contains(loops[looped_bbs[bb]].entries, bb)
                  //                             ? "green"
                  //                             : contains(loops[looped_bbs[bb]].escapes, bb) ?
                  //                             "red" : "white")
                  //                      :
                  "white"

          );
        }
        llvm::BasicBlock *cur = entry_bb;
        while (cur) {
          CFG_Node &node = bb_cfg[cur];
          if (node.next == NULL) break;
          u32 src_id = bb_to_id[cur];
          u32 dst_id = bb_to_id[node.next];
          fprintf(dotgraph, "%i -> %i [style=dotted];\n", src_id, dst_id);
          cur = node.next;
        }
        for (llvm::BasicBlock *bb : bbs) {
          u32       src_id = bb_to_id[bb];
          CFG_Node &node   = bb_cfg[bb];
          for (auto &dst_bb : node.out_bbs) {
            u32 dst_id = bb_to_id[dst_bb];
            if (is_back_edge(bb, dst_bb))
              fprintf(dotgraph, "%i -> %i [constraint = false, color=red];\n", src_id, dst_id);
            else
              fprintf(dotgraph, "%i -> %i [constraint = false];\n", src_id, dst_id);
          }
          //          if (node.cont != NULL) {
          //            u32 dst_id = bb_to_id[node.cont];
          //            fprintf(dotgraph, "%i -> %i [label=\"C\", style=dashed, constraint =
          //            false];\n", src_id,
          //                    dst_id);
          //          }
          //          if (node.merge != NULL) {
          //            u32 dst_id = bb_to_id[node.merge];
          //            fprintf(dotgraph, "%i -> %i [label=\"M\", style=dashed, constraint =
          //            false];\n", src_id,
          //                    dst_id);
          //          }
        }
      };
      dump_cfg();
      //            cur_fun->viewCFG();
    finish_function:
      continue;
    }
    //    llvm::StripDebugInfo(*module);
    //    module->dump();
    //    exit(1); //  NOCOMMIT;

    // @llvm/finish_module
    // Make a function that returns the size of private space required by this
    // module
    {
      llvm::Function *get_private_size = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_private_size", module.get());
      llvm::BasicBlock *bb = llvm::BasicBlock::Create(c, "entry", get_private_size);
      llvm::ReturnInst::Create(c, llvm::ConstantInt::get(c, llvm::APInt(32, private_storage_size)),
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
          llvm::Function::LinkageTypes::ExternalLinkage, "get_export_count", module.get());
      llvm::BasicBlock *bb          = llvm::BasicBlock::Create(c, "entry", get_export_count);
      u32               field_count = 0;
      for (auto &item : struct_types) {
        if (item.second.is_builtin) {
          ASSERT_ALWAYS(field_count == 0 && "Only one export structure per module is allowed");
          field_count = (u32)item.second.member_types.size();
        }
      }
      llvm::ReturnInst::Create(c, llvm::ConstantInt::get(c, llvm::APInt(32, field_count)), bb);
    }
    // Now enumerate all the items that are exported
    {
      llvm::Function *get_export_items = llvm::Function::Create(
          llvm::FunctionType::get(llvm::Type::getVoidTy(c), {llvm::Type::getInt32PtrTy(c)}, false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_export_items", module.get());
      llvm::Value *ptr_arg = get_export_items->getArg(0);
      NOTNULL(ptr_arg);
      llvm::BasicBlock *bb = llvm::BasicBlock::Create(c, "entry", get_export_items);
      std::unique_ptr<LLVM_IR_Builder_t> llvm_builder;
      llvm_builder.reset(new LLVM_IR_Builder_t(bb, llvm::NoFolder()));

      for (auto &item : struct_types) {
        u32 i = 0;
        if (item.second.is_builtin) {
          ASSERT_ALWAYS(i == 0);
          for (auto &member_builtin_id : item.second.member_builtins) {
            llvm::Value *gep = llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(i));
            llvm_builder->CreateStore(llvm_get_constant_i32(member_builtin_id), gep);
            i++;
          }
        }
      }
      llvm::ReturnInst::Create(c, bb);
    }
    {
      llvm::Function *get_input_count = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_input_count", module.get());
      llvm::BasicBlock *bb          = llvm::BasicBlock::Create(c, "entry", get_input_count);
      u32               input_count = 0;
      ito(input_sizes.size()) {
        if (input_sizes[i] != 0) input_count++;
      }
      llvm::ReturnInst::Create(c, llvm::ConstantInt::get(c, llvm::APInt(32, input_count)), bb);
    }
    {
      llvm::Function *get_input_stride = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_input_stride", module.get());
      llvm::BasicBlock *bb = llvm::BasicBlock::Create(c, "entry", get_input_stride);
      llvm::ReturnInst::Create(c, llvm::ConstantInt::get(c, llvm::APInt(32, input_storage_size)),
                               bb);
    }
    {
      llvm::Function *get_input_slots = llvm::Function::Create(
          llvm::FunctionType::get(llvm::Type::getVoidTy(c), {llvm::Type::getInt32PtrTy(c)}, false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_input_slots", module.get());
      llvm::Value *ptr_arg = get_input_slots->getArg(0);
      NOTNULL(ptr_arg);
      llvm::BasicBlock *                 bb = llvm::BasicBlock::Create(c, "entry", get_input_slots);
      std::unique_ptr<LLVM_IR_Builder_t> llvm_builder;
      llvm_builder.reset(new LLVM_IR_Builder_t(bb, llvm::NoFolder()));
      u32 k = 0;
      ito(input_sizes.size()) {
        if (input_sizes[i] != 0) {
          llvm::Value *gep_0 = llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3));
          llvm::Value *gep_1 = llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3 + 1));
          llvm::Value *gep_2 = llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3 + 2));
          llvm_builder->CreateStore(llvm_get_constant_i32(i), gep_0);
          llvm_builder->CreateStore(llvm_get_constant_i32(input_offsets[i]), gep_1);
          llvm_builder->CreateStore(llvm_get_constant_i32(input_formats[i]), gep_2);
          k++;
        }
      }
      llvm::ReturnInst::Create(c, bb);
    }
    {
      llvm::Function *get_output_count = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_output_count", module.get());
      llvm::BasicBlock *bb           = llvm::BasicBlock::Create(c, "entry", get_output_count);
      u32               output_count = 0;
      ito(output_sizes.size()) {
        if (output_sizes[i] != 0) output_count++;
      }
      llvm::ReturnInst::Create(c, llvm::ConstantInt::get(c, llvm::APInt(32, output_count)), bb);
    }
    {
      llvm::Function *get_output_stride = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_output_stride", module.get());
      llvm::BasicBlock *bb = llvm::BasicBlock::Create(c, "entry", get_output_stride);
      llvm::ReturnInst::Create(c, llvm::ConstantInt::get(c, llvm::APInt(32, output_storage_size)),
                               bb);
    }
    {
      llvm::Function *get_output_slots = llvm::Function::Create(
          llvm::FunctionType::get(llvm::Type::getVoidTy(c), {llvm::Type::getInt32PtrTy(c)}, false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_output_slots", module.get());
      llvm::Value *ptr_arg = get_output_slots->getArg(0);
      NOTNULL(ptr_arg);
      llvm::BasicBlock *bb = llvm::BasicBlock::Create(c, "entry", get_output_slots);
      std::unique_ptr<LLVM_IR_Builder_t> llvm_builder;
      llvm_builder.reset(new LLVM_IR_Builder_t(bb, llvm::NoFolder()));
      u32 k = 0;
      ito(output_sizes.size()) {
        if (output_sizes[i] != 0) {
          llvm::Value *gep_0 = llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3));
          llvm::Value *gep_1 = llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3 + 1));
          llvm::Value *gep_2 = llvm_builder->CreateGEP(ptr_arg, llvm_get_constant_i32(k * 3 + 2));
          llvm_builder->CreateStore(llvm_get_constant_i32(i), gep_0);
          llvm_builder->CreateStore(llvm_get_constant_i32(output_offsets[i]), gep_1);
          llvm_builder->CreateStore(llvm_get_constant_i32(output_formats[i]), gep_2);
          k++;
        }
      }
      llvm::ReturnInst::Create(c, bb);
    }
    {
      llvm::Function *get_subgroup_size = llvm::Function::Create(
          llvm::FunctionType::get(llvm::IntegerType::getInt32Ty(c), false),
          llvm::Function::LinkageTypes::ExternalLinkage, "get_subgroup_size", module.get());
      llvm::BasicBlock *bb = llvm::BasicBlock::Create(c, "entry", get_subgroup_size);
      llvm::ReturnInst::Create(c, llvm::ConstantInt::get(c, llvm::APInt(32, opt_subgroup_size)),
                               bb);
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
      for (llvm::Function &fun : module->functions()) {
        fun.setCallingConv(llvm::CallingConv::C);
        fun.removeFnAttr(llvm::Attribute::NoInline);
        fun.removeFnAttr(llvm::Attribute::OptimizeNone);
        fun.addFnAttr(llvm::Attribute::AlwaysInline);
      }
    }
    if (opt_debug_info)
      llvm_di_builder->finalize();
    else {
      llvm::StripDebugInfo(*module);
    }
    std::string              str;
    llvm::raw_string_ostream os(str);
    if (verifyModule(*module, &os)) {
      fprintf(stderr, "%s", os.str().c_str());
      abort();
    } else {
      // Create a function pass manager.
      std::unique_ptr<llvm::legacy::PassManager> FPM =
          std::make_unique<llvm::legacy::PassManager>();
      if (!opt_debug_info) {
        llvm::StripDebugInfo(*module);
      }
      if (opt_llvm_dump) {
        std::string              str;
        llvm::raw_string_ostream os(str);
        module->print(os, NULL);
        os.flush();
        fprintf(stdout, "%s", os.str().c_str());
      }
      if (opt_dump) {
        FPM->add(create_my_dump_pass(code_hash));
      }
      FPM->add(llvm::createFunctionInliningPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createReassociatePass());
      FPM->add(llvm::createGVNPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createGlobalDCEPass());
      FPM->add(llvm::createSROAPass());
      FPM->add(llvm::createEarlyCSEPass());
      FPM->add(llvm::createReassociatePass());
      FPM->add(llvm::createConstantPropagationPass());
      FPM->add(llvm::createDeadInstEliminationPass());
      FPM->add(llvm::createFunctionInliningPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createPromoteMemoryToRegisterPass());
      FPM->add(llvm::createAggressiveDCEPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createDeadInstEliminationPass());
      FPM->add(llvm::createSROAPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createPromoteMemoryToRegisterPass());
      FPM->add(llvm::createReassociatePass());
      FPM->add(llvm::createIPConstantPropagationPass());
      FPM->add(llvm::createDeadArgEliminationPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createPruneEHPass());
      FPM->add(llvm::createReversePostOrderFunctionAttrsPass());
      FPM->add(llvm::createConstantPropagationPass());
      FPM->add(llvm::createDeadInstEliminationPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createArgumentPromotionPass());
      FPM->add(llvm::createAggressiveDCEPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createJumpThreadingPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createSROAPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createTailCallEliminationPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createEarlyCSEPass());
      FPM->add(llvm::createFunctionInliningPass());
      FPM->add(llvm::createConstantPropagationPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createIPSCCPPass());
      FPM->add(llvm::createDeadArgEliminationPass());
      FPM->add(llvm::createAggressiveDCEPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createFunctionInliningPass());
      FPM->add(llvm::createArgumentPromotionPass());
      FPM->add(llvm::createSROAPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createReassociatePass());
      FPM->add(llvm::createLoopRotatePass());
      FPM->add(llvm::createLICMPass());
      FPM->add(llvm::createLoopUnswitchPass(false));
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createIndVarSimplifyPass());
      FPM->add(llvm::createLoopIdiomPass());
      FPM->add(llvm::createLoopDeletionPass());
      FPM->add(llvm::createLoopUnrollPass());
      FPM->add(llvm::createGVNPass());
      FPM->add(llvm::createMemCpyOptPass());
      FPM->add(llvm::createSCCPPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createJumpThreadingPass());
      FPM->add(llvm::createCorrelatedValuePropagationPass());
      FPM->add(llvm::createDeadStoreEliminationPass());
      FPM->add(llvm::createAggressiveDCEPass());
      FPM->add(llvm::createCFGSimplificationPass());
      FPM->add(llvm::createInstructionCombiningPass());
      FPM->add(llvm::createAggressiveInstCombinerPass());
      FPM->add(llvm::createFunctionInliningPass());
      FPM->add(llvm::createAggressiveDCEPass());
      FPM->add(llvm::createStripDeadPrototypesPass());
      FPM->add(llvm::createGlobalDCEPass());
      FPM->add(llvm::createGlobalOptimizerPass());
      FPM->add(llvm::createConstantMergePass());
      if (opt_dump) {
        FPM->add(create_my_dump_pass(code_hash));
      }
      FPM->run(*module);
      // @Debug
      if (0) {
        for (auto &fun : module->functions()) {
          if (stref_find(stref_s(fun.getName().str().c_str()), stref_s("spv_main")) >= 0)
            fun.viewCFG();
        }
      }
      return llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
      //      fprintf(stdout, "Module verified!\n");
    }
  }

  void parse_meta(const u32 *pCode, size_t codeSize) {
    this->code      = pCode;
    this->code_size = codeSize;
    code_hash       = hash_of(string_ref{(char const *)pCode, (size_t)codeSize * 4});
    if (opt_dump) {
      TMP_STORAGE_SCOPE;
      char buf[0x100];

      // Print spirv dumps
      {
        string_ref dir = stref_s("shader_dumps/spirv/");
        make_dir_recursive(dir);
        snprintf(buf, sizeof(buf), "%lx.spirv", code_hash);
        string_ref final_path = stref_concat(dir, stref_s(buf));
        snprintf(shader_dump_path, sizeof(shader_dump_path), "%.*s", (int)final_path.len,
                 final_path.ptr);
        spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_2);
        u32         options = SPV_BINARY_TO_TEXT_OPTION_NONE;
        options |= SPV_BINARY_TO_TEXT_OPTION_NO_HEADER;
        options |= SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES;
        spv_text       text;
        spv_diagnostic diagnostic = nullptr;
        spv_result_t   error =
            spvBinaryToText(context, pCode, code_size, options, &text, &diagnostic);
        spvContextDestroy(context);
        dump_file(stref_to_tmp_cstr(final_path), text->str, text->length);
        spvTextDestroy(text);
      }
    }
    ASSERT_ALWAYS(pCode[0] == spv::MagicNumber);
    ASSERT_ALWAYS(pCode[1] <= spv::Version);

    const u32 generator = pCode[2];
    const u32 ID_bound  = pCode[3];

    ASSERT_ALWAYS(pCode[4] == 0);

    const u32 *opStart = pCode + 5;
    const u32 *opEnd   = pCode + codeSize;
    pCode              = opStart;
    u32 cur_function   = 0;
#define CLASSIFY(id, TYPE) decl_types.push_back({id, TYPE});
    u32 spirv_line = 0;
    // First pass
    // Parse Meta data: types, decorations etc
    while (pCode < opEnd) {
      spirv_line++;
      u16     WordCount = pCode[0] >> spv::WordCountShift;
      spv::Op opcode    = spv::Op(pCode[0] & spv::OpCodeMask);
      u32     word1     = pCode[1];
      u32     word2     = WordCount > 2 ? pCode[2] : 0;
      u32     word3     = WordCount > 3 ? pCode[3] : 0;
      u32     word4     = WordCount > 4 ? pCode[4] : 0;
      u32     word5     = WordCount > 5 ? pCode[5] : 0;
      u32     word6     = WordCount > 6 ? pCode[6] : 0;
      u32     word7     = WordCount > 7 ? pCode[7] : 0;
      u32     word8     = WordCount > 8 ? pCode[8] : 0;
      u32     word9     = WordCount > 9 ? pCode[9] : 0;
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
        e.id              = word2;
        e.execution_model = (spv::ExecutionModel)word1;
        e.name            = (char const *)(pCode + 3);
        entries[e.id]     = e;
        break;
      }
      case spv::Op::OpMemberDecorate: {
        Member_Decoration dec;
        memset(&dec, 0, sizeof(dec));
        dec.target_id = word1;
        dec.member_id = word2;
        dec.type      = (spv::Decoration)word3;
        if (WordCount > 4) dec.param1 = word4;
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
        dec.type      = (spv::Decoration)word2;
        if (WordCount > 3) dec.param1 = word3;
        if (WordCount > 4) dec.param2 = word4;
        if (WordCount > 5) {
          UNIMPLEMENTED;
        }
        decorations[dec.target_id].push_back(dec);
        break;
      }
      case spv::Op::OpTypeVoid: {
        primitive_types[word1] = PrimitiveTy{.id = word1, .type = Primitive_t::Void};
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeFloat: {
        if (word2 == 16)
          primitive_types[word1] = PrimitiveTy{.id = word1, .type = Primitive_t::F16};
        else if (word2 == 32)
          primitive_types[word1] = PrimitiveTy{.id = word1, .type = Primitive_t::F32};
        else if (word2 == 64)
          primitive_types[word1] = PrimitiveTy{.id = word1, .type = Primitive_t::F64};
        else {
          UNIMPLEMENTED;
        }
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeBool: {
        primitive_types[word1] = PrimitiveTy{.id = word1, .type = Primitive_t::I1};
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeInt: {
        bool sign = word3 != 0;
        if (word2 == 8)
          primitive_types[word1] =
              PrimitiveTy{.id = word1, .type = (sign ? Primitive_t::I8 : Primitive_t::U8)};
        else if (word2 == 16)
          primitive_types[word1] =
              PrimitiveTy{.id = word1, .type = (sign ? Primitive_t::I16 : Primitive_t::U16)};
        else if (word2 == 32)
          primitive_types[word1] =
              PrimitiveTy{.id = word1, .type = (sign ? Primitive_t::I32 : Primitive_t::U32)};
        else if (word2 == 64)
          primitive_types[word1] =
              PrimitiveTy{.id = word1, .type = (sign ? Primitive_t::I64 : Primitive_t::U64)};
        else {
          UNIMPLEMENTED;
        }
        CLASSIFY(word1, DeclTy::PrimitiveTy);
        break;
      }
      case spv::Op::OpTypeVector: {
        VectorTy type;
        type.id             = word1;
        type.member_id      = word2;
        type.width          = word3;
        vector_types[word1] = type;
        CLASSIFY(type.id, DeclTy::VectorTy);
        break;
      }
      case spv::Op::OpTypeArray: {
        ArrayTy type;
        type.id            = word1;
        type.member_id     = word2;
        type.width_id      = word3;
        array_types[word1] = type;
        CLASSIFY(type.id, DeclTy::ArrayTy);
        break;
      }
      case spv::Op::OpTypeMatrix: {
        MatrixTy type;
        type.id             = word1;
        type.vector_id      = word2;
        type.width          = word3;
        matrix_types[word1] = type;
        CLASSIFY(type.id, DeclTy::MatrixTy);
        break;
      }
      case spv::Op::OpTypePointer: {
        PtrTy type;
        type.id            = word1;
        type.storage_class = (spv::StorageClass)word2;
        type.target_id     = word3;
        ptr_types[word1]   = type;
        CLASSIFY(type.id, DeclTy::PtrTy);
        break;
      }
      case spv::Op::OpTypeRuntimeArray: {
        // Just as any ptr
        PtrTy type;
        type.id            = word1;
        type.storage_class = spv::StorageClass::StorageClassMax;
        type.target_id     = word2;
        ptr_types[word1]   = type;
        CLASSIFY(type.id, DeclTy::RuntimeArrayTy);
        break;
      }
      case spv::Op::OpTypeStruct: {
        StructTy type;
        type.id         = word1;
        type.is_builtin = false;
        for (u16 i = 2; i < WordCount; i++) {
          type.member_types.push_back(pCode[i]);
          type.member_offsets.push_back(0);
          type.member_builtins.push_back(spv::BuiltIn::BuiltInMax);
        }
        ito(type.member_types.size()) {
          if (has_member_decoration(spv::Decoration::DecorationBuiltIn, type.id, i)) {
            type.is_builtin = true;
            type.member_builtins[i] =
                (spv::BuiltIn)find_member_decoration(spv::Decoration::DecorationBuiltIn, type.id, i)
                    .param1;
          } else {
            ASSERT_ALWAYS(type.is_builtin == false);
            type.member_offsets[i] =
                find_member_decoration(spv::Decoration::DecorationOffset, type.id, i).param1;
          }
        }
        type.size           = 0;
        struct_types[word1] = type;
        CLASSIFY(type.id, DeclTy::StructTy);
        break;
      }
      case spv::Op::OpTypeFunction: {
        FunTy &f = functypes[word1];
        f.id     = word1;
        for (u16 i = 3; i < WordCount; i++) f.params.push_back(pCode[i]);
        f.ret = word2;
        CLASSIFY(f.id, DeclTy::FunTy);
        break;
      }
      case spv::Op::OpTypeImage: {
        ImageTy type;
        type.id           = word1;
        type.sampled_type = word2;
        type.dim          = (spv::Dim)word3;
        type.depth        = word4 == 1;
        type.arrayed      = word5 != 0;
        type.ms           = word6 != 0;
        type.sampled      = word7;
        type.format       = (spv::ImageFormat)(word8);
        type.access       = WordCount > 8 ? (spv::AccessQualifier)(word9)
                                    : spv::AccessQualifier::AccessQualifierMax;
        images[word1] = type;
        CLASSIFY(type.id, DeclTy::ImageTy);
        break;
      }
      case spv::Op::OpTypeSampledImage: {
        Sampled_ImageTy type;
        type.id                = word1;
        type.sampled_image     = word2;
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
        var.id           = word2;
        var.type_id      = word1;
        var.storage      = (spv::StorageClass)word3;
        var.init_id      = word4;
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
        default: UNIMPLEMENTED_(get_cstr(var.storage));
        }
        CLASSIFY(var.id, DeclTy::Variable);
        break;
      }
      case spv::Op::OpFunction: {
        ASSERT_ALWAYS(cur_function == 0);
        Function fun;
        fun.id            = word2;
        fun.result_type   = word1;
        fun.function_type = word4;
        fun.spirv_line    = spirv_line;
        fun.control       = (spv::FunctionControlMask)word2;
        cur_function      = word2;
        functions[word2]  = fun;
        CLASSIFY(fun.id, DeclTy::Function);
        break;
      }
      case spv::Op::OpFunctionParameter: {
        ASSERT_ALWAYS(cur_function != 0);
        FunctionParameter fp;
        fp.id      = word2;
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
        c.id   = word2;
        c.type = word1;
        memcpy(&c.i32_val, &word3, 4);
        constants[word2] = c;
        CLASSIFY(c.id, DeclTy::Constant);
        break;
      }
      case spv::Op::OpConstantTrue: {
        Constant c;
        c.id             = word2;
        c.type           = word1;
        c.i32_val        = (u32)~0;
        constants[word2] = c;
        CLASSIFY(c.id, DeclTy::Constant);
        break;
      }
      case spv::Op::OpConstantFalse: {
        Constant c;
        c.id             = word2;
        c.type           = word1;
        c.i32_val        = 0;
        constants[word2] = c;
        CLASSIFY(c.id, DeclTy::Constant);
        break;
      }
      case spv::Op::OpConstantComposite: {
        ConstantComposite c;
        c.id   = word2;
        c.type = word1;
        ASSERT_ALWAYS(WordCount > 3);
        ito(WordCount - 3) { c.components.push_back(pCode[i + 3]); }
        constants_composite[c.id] = c;
        CLASSIFY(c.id, DeclTy::ConstantComposite);
        break;
      }
      case spv::Op::OpUndef: {
        Constant c;
        c.id             = word2;
        c.type           = word1;
        c.i32_val        = 0;
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
      case spv::Op::OpGroupMemberDecorate: UNIMPLEMENTED_(get_cstr(opcode));
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
      case spv::Op::OpSubgroupAvcMceGetDefaultInterBaseMultiReferencePenaltyINTEL:
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
      case spv::Op::OpSubgroupAvcMceSetSingleReferenceInterlacedFieldPolarityINTEL:
      case spv::Op::OpSubgroupAvcMceSetDualReferenceInterlacedFieldPolaritiesINTEL:
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
      case spv::Op::OpSubgroupAvcMceGetInterReferenceInterlacedFieldPolaritiesINTEL:
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
      case spv::Op::OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeMotionVectorsINTEL:
      case spv::Op::OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeDistortionsINTEL:
      case spv::Op::OpSubgroupAvcImeGetStreamoutSingleReferenceMajorShapeReferenceIdsINTEL:
      case spv::Op::OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeMotionVectorsINTEL:
      case spv::Op::OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeDistortionsINTEL:
      case spv::Op::OpSubgroupAvcImeGetStreamoutDualReferenceMajorShapeReferenceIdsINTEL:
      case spv::Op::OpSubgroupAvcImeGetBorderReachedINTEL:
      case spv::Op::OpSubgroupAvcImeGetTruncatedSearchIndicationINTEL:
      case spv::Op::OpSubgroupAvcImeGetUnidirectionalEarlySearchTerminationINTEL:
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
      default: UNIMPLEMENTED_(get_cstr(opcode));
      }
      pCode += WordCount;
    }
#undef CLASSIFY
    // TODO handle more cases like WASM_32 etc
    auto get_pointer_size   = []() { return sizeof(void *); };
    auto get_primitive_size = [](Primitive_t type) -> size_t {
      size_t size = 0;
      switch (type) {
      case Primitive_t::I1:
      case Primitive_t::I8:
      case Primitive_t::U8: size = 1; break;
      case Primitive_t::I16:
      case Primitive_t::U16: size = 2; break;
      case Primitive_t::I32:
      case Primitive_t::U32:
      case Primitive_t::F32: size = 4; break;
      case Primitive_t::I64:
      case Primitive_t::U64:
      case Primitive_t::F64: size = 8; break;
      default: UNIMPLEMENTED_(get_cstr(type));
      }
      ASSERT_ALWAYS(size != 0);
      return size;
    };
    std::function<size_t(u32)> get_size = [&](u32 member_type_id) -> size_t {
      size_t size = 0;
      ASSERT_ALWAYS(decl_types_table.find(member_type_id) != decl_types_table.end());
      DeclTy decl_type = decl_types_table.find(member_type_id)->second;
      // Limit to primitives and vectors and arrays
      ASSERT_ALWAYS(decl_type == DeclTy::PrimitiveTy || decl_type == DeclTy::ArrayTy ||
                    decl_type == DeclTy::MatrixTy || decl_type == DeclTy::RuntimeArrayTy ||
                    decl_type == DeclTy::VectorTy);
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
        VectorTy vtype           = vector_types[member_type_id];
        u32      vmember_type_id = vtype.member_id;
        size                     = get_size(vmember_type_id) * vtype.width;
        break;
      }
      case DeclTy::ArrayTy: {
        ASSERT_ALWAYS(contains(array_types, member_type_id));
        ArrayTy atype           = array_types[member_type_id];
        u32     amember_type_id = atype.member_id;
        u32     length          = 0;
        ASSERT_ALWAYS(contains(constants, atype.width_id));
        length = constants[atype.width_id].i32_val;
        ASSERT_ALWAYS(length != 0);
        size = get_size(amember_type_id) * length;
        break;
      }
      case DeclTy::MatrixTy: {
        ASSERT_ALWAYS(contains(matrix_types, member_type_id));
        MatrixTy type = matrix_types[member_type_id];
        size          = get_size(type.vector_id) * type.width;
        break;
      }
      default: UNIMPLEMENTED_(get_cstr(decl_type));
      }
      if (is_void) return 0;
      ASSERT_ALWAYS(size != 0);
      return size;
    };
    std::function<VkFormat(u32)> get_format = [&](u32 member_type_id) -> VkFormat {
      ASSERT_ALWAYS(decl_types_table.find(member_type_id) != decl_types_table.end());
      DeclTy decl_type = decl_types_table.find(member_type_id)->second;
      // Limit to primitives and vectors and arrays
      ASSERT_ALWAYS(decl_type == DeclTy::PrimitiveTy || decl_type == DeclTy::VectorTy);
      switch (decl_type) {
      case DeclTy::PrimitiveTy: {
        ASSERT_ALWAYS(contains(primitive_types, member_type_id));
        Primitive_t ptype = primitive_types[member_type_id].type;
        switch (ptype) {
        case Primitive_t::F32: return VkFormat::VK_FORMAT_R32_SFLOAT;
        default: UNIMPLEMENTED;
        }
        UNIMPLEMENTED;
      }
      case DeclTy::VectorTy: {
        ASSERT_ALWAYS(contains(vector_types, member_type_id));
        VectorTy vtype           = vector_types[member_type_id];
        u32      vmember_type_id = vtype.member_id;
        VkFormat member_format   = get_format(vmember_type_id);
        switch (member_format) {
        case VkFormat::VK_FORMAT_R32_SFLOAT:
          switch (vtype.width) {
          case 2: return VkFormat::VK_FORMAT_R32G32_SFLOAT;
          case 3: return VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
          case 4: return VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
          default: UNIMPLEMENTED;
          }
        default: UNIMPLEMENTED;
        }
        UNIMPLEMENTED;
      }
      default: UNIMPLEMENTED_(get_cstr(decl_type));
      }
      UNIMPLEMENTED;
    };
    for (auto &item : decl_types) {
      decl_types_table[item.first] = item.second;
    }
    auto get_pointee_size = [&](u32 ptr_type_id) {
      ASSERT_ALWAYS(decl_types_table[ptr_type_id] == DeclTy::PtrTy);
      PtrTy ptr_type = ptr_types[ptr_type_id];
      return get_size(ptr_type.target_id);
    };
    auto get_pointee_format = [&](u32 ptr_type_id) {
      ASSERT_ALWAYS(decl_types_table[ptr_type_id] == DeclTy::PtrTy);
      PtrTy ptr_type = ptr_types[ptr_type_id];
      return get_format(ptr_type.target_id);
    };
    for (auto &item : decl_types) {
      u32 type_id = item.first;
      switch (item.second) {
      case DeclTy::FunTy: type_sizes[type_id] = 0; break;
      case DeclTy::PtrTy:
      case DeclTy::RuntimeArrayTy: type_sizes[type_id] = get_pointer_size(); break;
      case DeclTy::StructTy: {
        ASSERT_HAS(struct_types);
        StructTy type = struct_types.find(type_id)->second;
        ASSERT_ALWAYS(type.member_offsets.size() > 0);
        u32 last_member_offset = type.member_offsets[type.member_offsets.size() - 1];
        type.size              = last_member_offset + (u32)get_size(type.member_types.back());
        type_sizes[type_id]    = type.size;
        break;
      }
      case DeclTy::Function:
      case DeclTy::Constant:
      case DeclTy::Variable:
      case DeclTy::ConstantComposite: break;
      case DeclTy::ImageTy:
      case DeclTy::SamplerTy:
      case DeclTy::Sampled_ImageTy: type_sizes[type_id] = 4; break;
      default: type_sizes[type_id] = get_size(type_id);
      }
    }
    // Calculate the layout of the input and ouput data needed for optimal
    // work of the shader
    {
      std::vector<u32> inputs;
      std::vector<u32> outputs;
      u32              max_input_location  = 0;
      u32              max_output_location = 0;
      // First pass figure out the number of input/output slots
      for (auto &item : decl_types) {
        if (item.second == DeclTy::Variable) {
          Variable var = variables[item.first];
          ASSERT_ALWAYS(var.id > 0);
          // Skip builtins here
          if (has_decoration(spv::Decoration::DecorationBuiltIn, var.id)) continue;
          if (decl_types_table[var.type_id] == DeclTy::PtrTy) {
            PtrTy ptr_ty = ptr_types[var.type_id];
            if (decl_types_table[ptr_ty.target_id] == DeclTy::StructTy) {
              StructTy struct_ty = struct_types[ptr_ty.target_id];
              // This is a structure with builtin members so no need for
              // location
              if (struct_ty.is_builtin) continue;
            }
          }
          if (var.storage == spv::StorageClass::StorageClassInput) {
            u32 location = find_decoration(spv::Decoration::DecorationLocation, var.id).param1;
            if (inputs.size() <= location) {
              inputs.resize(location + 1);
            }
            inputs[location]   = var.id;
            max_input_location = std::max(max_input_location, location);
          } else if (var.storage == spv::StorageClass::StorageClassOutput) {
            u32 location = find_decoration(spv::Decoration::DecorationLocation, var.id).param1;
            if (outputs.size() <= location) {
              outputs.resize(location + 1);
            }
            outputs[location]   = var.id;
            max_output_location = std::max(max_output_location, location);
          }
        }
      }
      u32 input_offset  = 0;
      u32 output_offset = 0;
      input_offsets.resize(max_input_location + 1);
      input_sizes.resize(max_input_location + 1);
      input_formats.resize(max_input_location + 1);
      output_offsets.resize(max_output_location + 1);
      output_sizes.resize(max_output_location + 1);
      output_formats.resize(max_output_location + 1);
      for (u32 id : inputs) {
        if (id > 0) {
          Variable var      = variables[id];
          u32      location = find_decoration(spv::Decoration::DecorationLocation, var.id).param1;
          // Align to 16 bytes
          if ((input_offset & 0xf) != 0) {
            input_offset = (input_offset + 0xf) & (~0xfu);
          }
          input_offsets[location] = input_offset;
          u32 size                = (u32)get_pointee_size(var.type_id);
          input_sizes[location]   = size;
          input_formats[location] = get_pointee_format(var.type_id);
          input_offset += size;
        }
      }
      for (u32 id : outputs) {
        if (id > 0) {
          Variable var      = variables[id];
          u32      location = find_decoration(spv::Decoration::DecorationLocation, var.id).param1;
          // Align to 16 bytes
          if ((output_offset & 0xf) != 0) {
            output_offset = (output_offset + 0xf) & (~0xfu);
          }
          output_offsets[location] = output_offset;
          u32 size                 = (u32)get_pointee_size(var.type_id);
          output_sizes[location]   = size;
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
      input_storage_size  = input_offset;
    }
    for (auto &item : global_variables) {
      Variable var = variables[item];
      if (var.storage == spv::StorageClass::StorageClassPrivate) {
        // Usually it's a pointer but is it always?
        ASSERT_ALWAYS(contains(ptr_types, var.type_id));
        PtrTy ptr             = ptr_types[var.type_id];
        u32   pointee_type_id = ptr.target_id;
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
void *compile_spirv(u32 const *pCode, size_t code_size) {
  Spirv_Builder builder;
  builder.parse_meta(pCode, code_size / 4);
  llvm::orc::ThreadSafeModule bundle = builder.build_llvm_module_vectorized();
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  //  llvm::InitializeAllTargetMCs();
  //  llvm::InitializeAllDisassemblers();

  llvm::ExitOnError ExitOnErr;

  // Create an LLJIT instance.
  std::unique_ptr<llvm::orc::LLJIT> J;
  Jitted_Shader *                   jitted_shader = new Jitted_Shader();
  if (builder.opt_dump) {
    J = ExitOnErr(
        llvm::orc::LLJITBuilder()
            .setCompileFunctionCreator(
                [&](llvm::orc::JITTargetMachineBuilder JTMB)
                    -> llvm::Expected<std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
                  auto TM = JTMB.createTargetMachine();
                  if (!TM) return TM.takeError();
                  return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(
                      std::move(*TM), &jitted_shader->obj_cache);
                })
            .create());
  } else {
    J = ExitOnErr(llvm::orc::LLJITBuilder().create());
  }
  ExitOnErr(J->addIRModule(std::move(bundle)));

  jitted_shader->jit = std::move(J);
  jitted_shader->init(builder.code_hash, builder.opt_debug_info, builder.opt_dump);
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
  u32 subgroup_size = (u32)atoi(argv[2]);
  ASSERT_ALWAYS(subgroup_size == 1 || subgroup_size == 4 || subgroup_size == 64);
  size_t size;
  auto * bytes = read_file(argv[1], &size);
  defer(tl_free(bytes));

  const u32 *   pCode    = (u32 *)bytes;
  size_t        codeSize = size / 4;
  Spirv_Builder builder;
  builder.opt_subgroup_size = subgroup_size;
  builder.parse_meta(pCode, codeSize);
  llvm::orc::ThreadSafeModule bundle = builder.build_llvm_module_vectorized();
  std::string                 str;
  llvm::raw_string_ostream    os(str);
  str.clear();
  bundle.getModuleUnlocked()->print(os, NULL);
  os.flush();
  fprintf(stdout, "%s", str.c_str());
}
#endif // S2L_EXE
