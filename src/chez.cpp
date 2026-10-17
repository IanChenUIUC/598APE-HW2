#include "node_detail.h"
#include <chez.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <node.h>
#include <program.h>
#include <scheme.h>
#include <string>

namespace genetic {
namespace {

using kernel_t = void (*)(const float *data, float *y_pred, uint64_t n_rows);

ptr top(const char *name) { return Stop_level_value(Sstring_to_symbol(name)); }

/** Returns the Scheme procedure that compiles source text to a locked
 * foreign-callable and returns its entry point. Boots Chez on first use. */
ptr compiler() {
  static ptr proc = [] {
    Sscheme_init(nullptr);
    Sregister_boot_file(CHEZ_BOOT "/petite.boot");
    Sregister_boot_file(CHEZ_BOOT "/scheme.boot");
    Sbuild_heap(nullptr, nullptr);
    ptr port = Scall1(top("open-input-string"),
                      Sstring("(begin (optimize-level 3)"
                              "  (lambda (s)"
                              "    (let ([c (eval (read (open-input-string s)))])"
                              "      (lock-object c)"
                              "      (foreign-callable-entry-point c))))"));
    ptr form = Scall1(top("read"), port);
    ptr p = Scall1(top("eval"), form);
    Slock_object(p);
    return p;
  }();
  return proc;
}

std::string flonum(float v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%#.9g", v);
  return buf;
}

/** Appends the expression for the subtree rooted at `nodes[i]` and returns the
 * index just past it. */
int emit(const node *nodes, int i, std::string &out) {
  const node &n = nodes[i++];
  switch (n.t) {
  case node::type::variable:
    out += "(foreign-ref 'float data (fx* 4 (fx+ (fx* " +
           std::to_string(n.u.fid) + " n) i)))";
    return i;
  case node::type::constant:
    out += flonum(n.u.val);
    return i;
  case node::type::add:
    out += "(fl+ ";
    break;
  case node::type::sub:
    out += "(fl- ";
    break;
  case node::type::mul:
    out += "(fl* ";
    break;
  case node::type::fdim:
    out += "(let ([d (fl- ";
    i = emit(nodes, i, out);
    out += " ";
    i = emit(nodes, i, out);
    out += ")]) (if (fl> d 0.0) d 0.0))";
    return i;
  case node::type::abs:
    out += "(flabs ";
    break;
  case node::type::sin:
    out += "(flsin ";
    break;
  case node::type::cos:
    out += "(flcos ";
    break;
  case node::type::log:
    out += "(let ([v (flabs ";
    i = emit(nodes, i, out);
    out += ")]) (if (fl< v " + flonum(detail::MIN_VAL) + ") 0.0 (fllog v)))";
    return i;
  case node::type::sq:
    out += "(let ([v ";
    i = emit(nodes, i, out);
    out += "]) (fl* v v))";
    return i;
  case node::type::sqrt:
    out += "(flsqrt (flabs ";
    i = emit(nodes, i, out);
    out += "))";
    return i;
  default:
    fprintf(stderr, "chez: unsupported node type %u\n",
            static_cast<unsigned>(n.t));
    abort();
  }
  i = emit(nodes, i, out);
  if (detail::arity(n.t) > 1) {
    out += " ";
    i = emit(nodes, i, out);
  }
  out += ")";
  return i;
}

std::string codegen(const program &p) {
  std::string out = "(foreign-callable (lambda (data y n)"
                    " (do ([i 0 (fx+ i 1)]) ((fx= i n))"
                    " (foreign-set! 'float y (fx* 4 i) ";
  emit(p.nodes, 0, out);
  out += "))) (uptr uptr size_t) void)";
  return out;
}

kernel_t compile(const std::string &src) {
  ptr proc = compiler();
  ptr entry = Scall1(proc, Sstring(src.c_str()));
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
    kernel_t kernel = compile(codegen(progs[pid]));
    kernel(data, y_pred + pid * n_rows, n_rows);
    release(kernel);
  }
}

} // namespace chez
} // namespace genetic
