#include "node_detail.h"
#include <chez.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <node.h>
#include <program.h>
#include <scheme.h>

namespace genetic {
namespace {

using kernel_t = void (*)(const float *data, float *y_pred, uint64_t n_rows);

ptr top(const char *name) { return Stop_level_value(Sstring_to_symbol(name)); }

ptr sym(const char *name) { return Sstring_to_symbol(name); }

ptr list(std::initializer_list<ptr> xs) {
  ptr l = Snil;
  for (auto it = std::rbegin(xs); it != std::rend(xs); ++it)
    l = Scons(*it, l);
  return l;
}

/** Returns `compile-kernel` from src/chez.ss, which compiles a program tree to
 * a locked foreign-callable and returns its entry point. Boots Chez on first
 * use. */
ptr compiler() {
  static ptr proc = [] {
    Sscheme_init(nullptr);
    Sregister_boot_file(CHEZ_BOOT "/petite.boot");
    Sregister_boot_file(CHEZ_BOOT "/scheme.boot");
    Sbuild_heap(nullptr, nullptr);
    Scall1(top("load"), Sstring("src/chez.ss"));
    ptr p = top("compile-kernel");
    Slock_object(p);
    return p;
  }();
  return proc;
}

/** Returns the tree for the subtree rooted at `nodes[i]` and advances `i` past
 * it. */
ptr emit(const node *nodes, int &i) {
  const node &n = nodes[i++];
  const char *op;
  switch (n.t) {
  case node::type::variable:
    return list({sym("var"), Sfixnum(n.u.fid)});
  case node::type::constant:
    return Sflonum(n.u.val);
  case node::type::add:
    op = "add";
    break;
  case node::type::sub:
    op = "sub";
    break;
  case node::type::mul:
    op = "mul";
    break;
  case node::type::fdim:
    op = "fdim";
    break;
  case node::type::abs:
    op = "abs";
    break;
  case node::type::sin:
    op = "sin";
    break;
  case node::type::cos:
    op = "cos";
    break;
  case node::type::log:
    op = "log";
    break;
  case node::type::sq:
    op = "sq";
    break;
  case node::type::sqrt:
    op = "sqrt";
    break;
  default:
    fprintf(stderr, "chez: unsupported node type %u\n",
            static_cast<unsigned>(n.t));
    abort();
  }
  ptr a = emit(nodes, i);
  if (detail::arity(n.t) > 1) {
    ptr b = emit(nodes, i);
    return list({sym(op), a, b});
  }
  return list({sym(op), a});
}

kernel_t compile(const program &p) {
  ptr proc = compiler();
  int i = 0;
  ptr tree = emit(p.nodes, i);
  ptr entry = Scall2(proc, tree, Sflonum(detail::MIN_VAL));
  return reinterpret_cast<kernel_t>(Sunsigned_value(entry));
}

void release(kernel_t kernel) {
  Sunlock_object(
      Sforeign_callable_code_object(reinterpret_cast<uptr>(kernel)));
}

} // namespace

namespace chez {

void execute(const program_t progs, const float *data, float *y_pred,
             const uint64_t n_rows, const uint64_t n_progs) {
  for (uint64_t pid = 0; pid < n_progs; ++pid) {
    kernel_t kernel = compile(progs[pid]);
    kernel(data, y_pred + pid * n_rows, n_rows);
    release(kernel);
  }
}

} // namespace chez
} // namespace genetic
