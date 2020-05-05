#define UTILS_IMPL
#include "utils.hpp"

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
#include <llvm/IR/Module.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include "ll_stdlib.h"

#include <map>
#include <set>

struct List {
  string_ref symbol = {};
  u64        id     = 0;
  List *     child  = NULL;
  List *     next   = NULL;
  string_ref get_symbol() {
    ASSERT_ALWAYS(nonempty());
    return symbol;
  }
  bool nonempty() { return symbol.ptr != 0 && symbol.len != 0; }
  bool cmp_symbol(char const *str) {
    if (symbol.ptr == NULL) return false;
    return symbol == stref_s(str);
  }
  bool has_child(char const *name) { return child != NULL && child->cmp_symbol(name); }
  void match_children(char const *name, std::function<void(List *)> on_match) {
    if (child != NULL) {
      if (child->cmp_symbol(name)) {
        on_match(child);
      }
      child->match_children(name, on_match);
    }
    if (next != NULL) {
      next->match_children(name, on_match);
    }
  }
  List *get(u32 i) {
    List *cur = this;
    while (i != 0) {
      NOTNULL(cur)
      cur = cur->next;
      i -= 1;
    }
    return cur;
  }

  int ATTR_USED dump(u32 indent = 0) const {
    ito(indent) fprintf(stdout, " ");
    if (symbol.ptr != NULL) {
      fprintf(stdout, "%.*s\n", (i32)symbol.len, symbol.ptr);
    } else {
      fprintf(stdout, "$\n");
    }
    if (child != NULL) {
      child->dump(indent + 2);
    }
    if (next != NULL) {
      next->dump(indent);
    }
    fflush(stdout);
    return 0;
  }
};

static Temporary_Storage<List> list_storage = Temporary_Storage<List>::create((1 << 20));
static Temporary_Storage<>     ts           = Temporary_Storage<>::create(128 * (1 << 20));

void dump_list_graph(List *root) {
  FILE *dotgraph = fopen("list.dot", "wb");
  fprintf(dotgraph, "digraph {\n");
  //  fprintf(dotgraph, "rankdir=\"LR\";\n");
  fprintf(dotgraph, "node [shape=record];\n");
  ts.enter_scope();
  defer(ts.exit_scope());
  List **stack        = (List **)ts.alloc(sizeof(List *) * (1 << 10));
  u32    stack_cursor = 0;
  List * cur          = root;
  u64    null_id      = 0xffffffffull;
  while (cur != NULL || stack_cursor != 0) {
    if (cur == NULL) {
      cur = stack[--stack_cursor];
    }
    ASSERT_ALWAYS(cur != NULL);
    if (cur->symbol.ptr != NULL) {
      ASSERT_ALWAYS(cur->symbol.len != 0);
      fprintf(dotgraph, "%lu [label = \"%.*s\", shape = record];\n", cur->id, (int)cur->symbol.len,
              cur->symbol.ptr);
    } else {
      fprintf(dotgraph, "%lu [label = \"$\", shape = record, color=red];\n", cur->id);
    }
    if (cur->next == NULL) {
      fprintf(dotgraph, "%lu [label = \"nil\", shape = record, color=blue];\n", null_id);
      fprintf(dotgraph, "%lu -> %lu [label = \"next\"];\n", cur->id, null_id);
      null_id++;
    } else
      fprintf(dotgraph, "%lu -> %lu [label = \"next\"];\n", cur->id, cur->next->id);

    if (cur->child != NULL) {
      if (cur->next != NULL) stack[stack_cursor++] = cur->next;
      fprintf(dotgraph, "%lu -> %lu [label = \"child\"];\n", cur->id, cur->child->id);
      cur = cur->child;
    } else {
      cur = cur->next;
    }
  }
  fprintf(dotgraph, "}\n");
  fflush(dotgraph);
  fclose(dotgraph);
}

List *parse(string_ref text) {

  List *root = list_storage.alloc_zero(1);
  List *cur  = root;
  ts.enter_scope();
  defer(ts.exit_scope());
  List **stack        = (List **)ts.alloc(sizeof(List *) * (1 << 10));
  u32    stack_cursor = 0;
  enum class State : char {
    UNDEFINED = 0,
    SAW_QUOTE,
    SAW_LPAREN,
    SAW_RPAREN,
    SAW_PRINTABLE,
    SAW_SEPARATOR,
  };
  u32   i  = 0;
  u64   id = 1;
  State state_table[0x100];
  memset(state_table, 0, sizeof(state_table));
  for (u8 j = 0x20; j <= 0x7f; j++) state_table[j] = State::SAW_PRINTABLE;
  state_table['(']  = State::SAW_LPAREN;
  state_table[')']  = State::SAW_RPAREN;
  state_table['"']  = State::SAW_QUOTE;
  state_table[' ']  = State::SAW_SEPARATOR;
  state_table['\n'] = State::SAW_SEPARATOR;
  state_table['\t'] = State::SAW_SEPARATOR;
  state_table['\r'] = State::SAW_SEPARATOR;

  auto next_item = [&]() {
    List *next = list_storage.alloc_zero(1);
    next->id   = id++;
    if (cur != NULL) cur->next = next;
    cur = next;
  };

  auto push_item = [&]() {
    List *new_head = list_storage.alloc_zero(1);
    new_head->id   = id++;
    if (cur != NULL) {
      stack[stack_cursor++] = cur;
      cur->child            = new_head;
    }
    cur = new_head;
  };

  auto pop_item = [&]() -> bool {
    if (stack_cursor == 0) {
      return false;
    }
    cur = stack[--stack_cursor];
    return true;
  };

  auto append_char = [&]() {
    if (cur->symbol.ptr == NULL) { // first character for that item
      cur->symbol.ptr = text.ptr + i;
    }
    cur->symbol.len++;
  };

  auto cur_non_empty = [&]() { return cur != NULL && cur->symbol.len != 0; };
  auto cur_has_child = [&]() { return cur != NULL && cur->child != NULL; };

  i = 0;
  while (i < text.len) {
    char  c     = text.ptr[i];
    State state = state_table[(u8)c];
    switch (state) {
    case State::UNDEFINED: goto exit_loop;
    case State::SAW_QUOTE: {
      if (cur_non_empty()) next_item();
      i += 1;
      while ((c = text.ptr[i]) != '"') {
        append_char();
        i += 1;
      }
      next_item();
      break;
    }
    case State::SAW_LPAREN: {
      if (cur_has_child()) next_item();
      push_item();
      break;
    }
    case State::SAW_RPAREN: {
      if (pop_item() == false) goto exit_loop;
      break;
    }
    case State::SAW_SEPARATOR: {
      if (cur_non_empty()) next_item();
      break;
    }
    case State::SAW_PRINTABLE: {
      if (cur_has_child()) next_item();
      append_char();
      break;
    }
    }
    i += 1;
  }
exit_loop:
  (void)0;
  return root;
}

char *read_file_tmp(char const *filename) {
  FILE *text_file = fopen(filename, "rb");
  ASSERT_ALWAYS(text_file);
  fseek(text_file, 0, SEEK_END);
  long fsize = ftell(text_file);
  fseek(text_file, 0, SEEK_SET);
  size_t size = (size_t)fsize;
  char * data = (char *)ts.alloc((size_t)fsize);
  fread(data, 1, (size_t)fsize, text_file);
  fclose(text_file);
  return data;
}

bool parse_decimal_int(char const *str, size_t len, int32_t *result) {
  int32_t  final = 0;
  int32_t  pow   = 1;
  int32_t  sign  = 1;
  uint32_t i     = 0;
  if (str[0] == '-') {
    sign = -1;
    i    = 1;
  }
  for (; i < len; ++i) {
    switch (str[len - 1 - i]) {
    case '0': break;
    case '1': final += 1 * pow; break;
    case '2': final += 2 * pow; break;
    case '3': final += 3 * pow; break;
    case '4': final += 4 * pow; break;
    case '5': final += 5 * pow; break;
    case '6': final += 6 * pow; break;
    case '7': final += 7 * pow; break;
    case '8': final += 8 * pow; break;
    case '9': final += 9 * pow; break;
    default: return false;
    }
    pow *= 10;
  }
  *result = sign * final;
  return true;
}
bool parse_float(char const *str, size_t len, float *result) {
  float    final = 0.0f;
  uint32_t i     = 0;
  float    sign  = 1.0f;
  if (str[0] == '-') {
    sign = -1.0f;
    i    = 1;
  }
  for (; i < len; ++i) {
    if (str[i] == '.') break;
    switch (str[i]) {
    case '0': final = final * 10.0f; break;
    case '1': final = final * 10.0f + 1.0f; break;
    case '2': final = final * 10.0f + 2.0f; break;
    case '3': final = final * 10.0f + 3.0f; break;
    case '4': final = final * 10.0f + 4.0f; break;
    case '5': final = final * 10.0f + 5.0f; break;
    case '6': final = final * 10.0f + 6.0f; break;
    case '7': final = final * 10.0f + 7.0f; break;
    case '8': final = final * 10.0f + 8.0f; break;
    case '9': final = final * 10.0f + 9.0f; break;
    default: return false;
    }
  }
  i++;
  float pow = 1.0e-1f;
  for (; i < len; ++i) {
    switch (str[i]) {
    case '0': break;
    case '1': final += 1.0f * pow; break;
    case '2': final += 2.0f * pow; break;
    case '3': final += 3.0f * pow; break;
    case '4': final += 4.0f * pow; break;
    case '5': final += 5.0f * pow; break;
    case '6': final += 6.0f * pow; break;
    case '7': final += 7.0f * pow; break;
    case '8': final += 8.0f * pow; break;
    case '9': final += 9.0f * pow; break;
    default: return false;
    }
    pow *= 1.0e-1f;
  }
  *result = sign * final;
  return true;
}

#define LOOKUP_FN(name)                                                                            \
  llvm::Function *name = module->getFunction(#name);                                               \
  ASSERT_ALWAYS(name != NULL);
#define LOOKUP_TY(name)                                                                            \
  llvm::Type *name = module->getTypeByName(#name);                                                 \
  ASSERT_ALWAYS(name != NULL);

void llvm_gen_module(List *root) {
  std::unique_ptr<llvm::LLVMContext> context(new llvm::LLVMContext());
  auto &                             c                     = *context;
  auto                               llvm_get_constant_i32 = [&c](u32 a) {
    return llvm::ConstantInt::get(llvm::IntegerType::getInt32Ty(c), a);
  };
  auto llvm_get_constant_i64 = [&c](u64 a) {
    return llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(c), a);
  };

  llvm::SMDiagnostic error;
  using LLVM_IR_Builder_t = llvm::IRBuilder<llvm::NoFolder>;
  root->match_children("module", [&](List *module_node) {
    // Found a module
    NOTNULL(module_node);
    ASSERT_ALWAYS(module_node->cmp_symbol("module"))
    // Create a new module
    auto mbuf = llvm::MemoryBuffer::getMemBuffer(
        llvm::StringRef((char *)ll_stdlib_bc, ll_stdlib_bc_len), "", false);
    std::unique_ptr<llvm::Module> module = llvm::parseIR(*mbuf.get(), error, c);
    ASSERT_ALWAYS(module);
    std::map<std::string, llvm::GlobalVariable *> global_strings;
    auto                                          lookup_string = [&](std::string str) {
      if (contains(global_strings, str)) return global_strings[str];
      llvm::Constant *      msg = llvm::ConstantDataArray::getString(c, str.c_str(), true);
      llvm::GlobalVariable *msg_glob = new llvm::GlobalVariable(
          *module, msg->getType(), true, llvm::GlobalValue::InternalLinkage, msg);
      global_strings[str] = msg_glob;
      return msg_glob;
    };

    LOOKUP_FN(ll_printf);

    llvm::Function *                     cur_fun   = NULL;
    llvm::BasicBlock *                   cur_bb    = NULL;
    llvm::BasicBlock *                   alloca_bb = NULL;
    std::map<std::string, llvm::Value *> symbol_table;
    std::unique_ptr<LLVM_IR_Builder_t>   llvm_builder;
    auto                                 parse_int = [&](List *l) {
      NOTNULL(l);
      ASSERT_ALWAYS(l->nonempty());
      int32_t i = 0;
      ASSERT_ALWAYS(parse_decimal_int(l->symbol.ptr, l->symbol.len, &i));
      return i;
    };
    std::function<llvm::Type *(List * l)> get_type = [&](List *l) -> llvm::Type * {
      NOTNULL(l);
      if (l->nonempty() == false) {
        NOTNULL(l->child);
        return get_type(l->child);
      }
      ASSERT_ALWAYS(l->nonempty());
      if (l->cmp_symbol("i32")) {
        return llvm::Type::getInt32Ty(c);
      } else if (l->cmp_symbol("i64")) {
        return llvm::Type::getInt64Ty(c);
      } else if (l->cmp_symbol("f32")) {
        return llvm::Type::getFloatTy(c);
      } else if (l->cmp_symbol("i16")) {
        return llvm::Type::getInt16Ty(c);
      } else if (l->cmp_symbol("i8")) {
        return llvm::Type::getInt8Ty(c);
      } else if (l->cmp_symbol("i1")) {
        return llvm::Type::getInt1Ty(c);
      } else if (l->cmp_symbol("pointer")) {
        return llvm::PointerType::get(get_type(l->next), 0);
      } else if (l->cmp_symbol("vector")) {
        return llvm::VectorType::get(get_type(l->next), parse_int(l->next->next));
      } else if (l->cmp_symbol("array")) {
        return llvm::ArrayType::get(get_type(l->next), parse_int(l->next->next));
      } else {
        return NULL;
      }
    };
    auto lookup_val = [&](string_ref name) {
      ASSERT_ALWAYS(contains(symbol_table, stref_to_tmp_cstr(name)));
      return symbol_table[stref_to_tmp_cstr(name)];
    };
    auto get_val = [&](List *l) -> llvm::Value * {
      ASSERT_ALWAYS(l->nonempty());
      llvm::Value *ref = lookup_val(l->get_symbol());
      return ref; // llvm_builder->CreateLoad(ref);
    };
    std::function<llvm::Value *(List *)> emit_instr = [&](List *instr) -> llvm::Value * {
      NOTNULL(instr);
      NOTNULL(cur_bb);
      if (!instr->nonempty()) {
        NOTNULL(instr->child);
        return emit_instr(instr->child);
      }
      ASSERT_ALWAYS(instr->nonempty());

      SmallArray<List *, 8> args;
      args.init();
      defer(args.release(););
      {
        List *arg = instr->next;
        while (arg != NULL) {
          args.push(arg);
          arg = arg->next;
        }
      }
      if (llvm::Type *ty = get_type(instr)) {
        ASSERT_ALWAYS(args.get_size() == 1);
        llvm::Value *val = llvm_get_constant_i32(parse_int(args[0]));
        return val;
      } else if (contains(symbol_table, stref_to_tmp_cstr(instr->symbol)) &&
                 instr->child == NULL) {
        return get_val(instr);
      } else if (instr->symbol == stref_s("add")) {
        ASSERT_ALWAYS(args.get_size() == 2);
        llvm::Value *val_0 = emit_instr(args[0]);
        llvm::Value *val_1 = emit_instr(args[1]);
        return llvm_builder->CreateFAdd(val_0, val_1);
      } else if (instr->symbol == stref_s("load")) {
        ASSERT_ALWAYS(args.get_size() == 1);
        return llvm_builder->CreateLoad(emit_instr(args[0]));
      } else if (instr->symbol == stref_s("gep")) {
        ASSERT_ALWAYS(args.get_size() > 1);
        llvm::SmallVector<llvm::Value *, 4> chain;
        ito(args.get_size() - 1) { chain.push_back(llvm_get_constant_i32(parse_int(args[i + 1]))); }
        return llvm_builder->CreateGEP(emit_instr(args[0]), chain);
      } else if (instr->symbol == stref_s("store")) {
        ASSERT_ALWAYS(args.get_size() == 2);
        return llvm_builder->CreateStore(emit_instr(args[0]), emit_instr(args[1]));
      } else if (instr->symbol == stref_s("alloca")) {
        ASSERT_ALWAYS(args.get_size() == 1);
        return llvm_builder->CreateAlloca(get_type(args[0]));
      } else if (instr->symbol == stref_s("let")) {
        ASSERT_ALWAYS(args.get_size() == 2);
        string_ref   val_name                     = args[0]->get_symbol();
        llvm::Value *val                          = emit_instr(args[1]);
        symbol_table[stref_to_tmp_cstr(val_name)] = val;
      } else if (instr->symbol == stref_s("printf")) {
        ASSERT_ALWAYS(args.get_size() > 1);
        List *fmt = args[0];
        llvm_builder->CreateCall(ll_printf, {lookup_string(stref_to_tmp_cstr(fmt->symbol))});
      } else if (instr->symbol == stref_s("ret")) {
        if (args.get_size() > 0) {
          ASSERT_ALWAYS(args.get_size() == 2);
          llvm::Type * ret_type = get_type(args[0]);
          llvm::Value *ret_val  = llvm_get_constant_i32(parse_int(args[1]));
          llvm::ReturnInst::Create(c, ret_val, cur_bb);
        } else {
          llvm::ReturnInst::Create(c, NULL, cur_bb);
        }
        cur_bb = NULL;
        llvm_builder.release();
      } else {
        UNIMPLEMENTED;
      }
      return NULL;
    };
    module_node->match_children("function", [&](List *func_node) {
      ASSERT_ALWAYS(cur_fun == NULL);
      List *return_type = func_node->get(1);
      List *name        = func_node->get(2);
      List *args        = func_node->get(3);
      tl_alloc_tmp_enter();
      defer(tl_alloc_tmp_exit());
      {

        llvm::SmallVector<llvm::Type *, 4> argv;
        llvm::SmallVector<std::string, 4>  argv_names;
        ASSERT_ALWAYS(args->child != NULL);
        List *arg = args->child;
        while (arg != NULL) {
          NOTNULL(arg->child);
          List *arg_type = arg->child->get(0);
          List *arg_name = arg->child->get(1);
          argv.push_back(get_type(arg_type));
          argv_names.push_back(stref_to_tmp_cstr(arg_name->symbol));
          //          arg_name->dump();
          arg = arg->next;
        }
        cur_fun =
            llvm::Function::Create(llvm::FunctionType::get(get_type(return_type), argv, false),
                                   llvm::Function::LinkageTypes::ExternalLinkage,
                                   llvm::Twine(stref_to_tmp_cstr(name->symbol)), *module);
        alloca_bb    = llvm::BasicBlock::Create(c, "allocas", cur_fun);
        symbol_table = {};
        ito(argv.size()) {
          llvm::Value *alloca = new llvm::AllocaInst(argv[i], 0, argv_names[i], alloca_bb);
          new llvm::StoreInst(cur_fun->getArg(i), alloca, alloca_bb);
        }
      }
      bool first_bb = true;
      func_node->match_children("label", [&](List *label_node) {
        ASSERT_ALWAYS(cur_bb == NULL);
        List *label_name = label_node->get(1);
        List *label_args = label_node->get(2);
        List *instr      = label_node->get(3);
        cur_bb = llvm::BasicBlock::Create(c, stref_to_tmp_cstr(label_name->symbol), cur_fun);
        if (first_bb) {
          llvm::BranchInst::Create(cur_bb, alloca_bb);
          first_bb = false;
        }
        llvm_builder.reset(new LLVM_IR_Builder_t(cur_bb, llvm::NoFolder()));
        while (instr != NULL) {
          tl_alloc_tmp_enter();
          defer(tl_alloc_tmp_exit());
          NOTNULL(instr->child);
          emit_instr(instr->child);
          instr = instr->next;
        }
        cur_bb = NULL;
      });
      cur_fun = NULL;
    });
    llvm::StripDebugInfo(*module);
    std::string              str;
    llvm::raw_string_ostream os(str);
    str.clear();
    module->print(os, NULL);
    os.flush();
    fprintf(stdout, "%s", str.c_str());
  });
}

int main(int argc, char **argv) {
  ts.enter_scope();
  defer(ts.exit_scope());
  //  ASSERT_ALWAYS(argc == 2);
  // argv[1]
  char const *text = R"(
  (module
    (function i32 main (
                    (i32 argc)
                    ((pointer (pointer i8)) argv)
                   )
      (label entry ()
        (let a (alloca (vector f32 4)))
        (let b (alloca (vector f32 4)))
        (let c (add (load a) (load b)))
        (store c a)
        (let c (load (gep a 0 1)))
        (printf "the number of arguments: %i\n" argc)
        (printf "the first argument: %s\n" (at argv 0))
        (let i_ptr (alloca i32))
        (store (i32 0) i_ptr)
        (ret i32 0)
      )
      (label loop_entry ()
        (let i (load i_ptr))
      )
    )
  )
  )";
  //  (let a (makevector f32 0.0 0.0 0.1 0.2))
  //        (let b (makevector f32 0.1 0.2 0.3 3.4))
  //        (let c (fcmplt a b))
  //        (dump c)
  List *root = parse(stref_s(text));
  dump_list_graph(root);
  llvm_gen_module(root);
  return 0;
}
