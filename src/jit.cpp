#include "jit/stencils.h"
#include "node_detail.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <jit.h>
#include <node.h>
#include <program.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace genetic {
namespace jit {
namespace {

using kernel_t = void (*)(const float *x, float *y);

/** Rows evaluated per call of a compiled program. */
constexpr uint64_t BLOCK = 8;

/** Registers available to hold live values. */
constexpr int REGISTERS = 8;

/** Executable memory holding `rodata`, then the code of one program. */
class buffer {
public:
  /** Makes the memory writable, with room for `code_size` bytes of code. */
  uint8_t *open(size_t code_size) {
    const size_t size = CODE + code_size;
    if (size > size_) {
      if (mem_)
        munmap(mem_, size_);
      const size_t page = sysconf(_SC_PAGESIZE);
      size_ = (size + page - 1) / page * page;
      void *mem = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (mem == MAP_FAILED) {
        perror("jit: mmap");
        abort();
      }
      mem_ = static_cast<uint8_t *>(mem);
      std::memcpy(mem_, rodata, sizeof(rodata));
    } else {
      protect(PROT_READ | PROT_WRITE);
    }
    return mem_ + CODE;
  }

  /** Makes the memory executable and returns the code as a function. */
  kernel_t close() {
    protect(PROT_READ | PROT_EXEC);
    return reinterpret_cast<kernel_t>(mem_ + CODE);
  }

  const uint8_t *rodata_address() const { return mem_; }

private:
  static constexpr size_t CODE = (sizeof(rodata) + 63) / 64 * 64;

  void protect(int prot) {
    if (mprotect(mem_, size_, prot) != 0) {
      perror("jit: mprotect");
      abort();
    }
  }

  uint8_t *mem_ = nullptr;
  size_t size_ = 0;
};

uint64_t address(float (*f)(float)) { return reinterpret_cast<uint64_t>(f); }

/** Compiles one program into a buffer. */
class compiler {
public:
  compiler(const program &p, uint64_t n_rows)
      : nodes_(p.nodes), n_rows_(n_rows), end_(p.len), need_(p.len) {
    ok_ = analyze(0) == p.len && need_[0] <= REGISTERS;
  }

  /** Whether every node has a stencil and the program fits in registers. */
  bool ok() const { return ok_; }

  kernel_t compile(buffer &buf) {
    at_ = buf.open(nodes_size() * max_size + stencils::store.size);
    rodata_ = buf.rodata_address();
    emit(0, 0);
    put(stencils::store, 0);
    return buf.close();
  }

private:
  int nodes_size() const { return static_cast<int>(end_.size()); }

  /** Records the end and register need of the subtree at `i`, and returns
   * its end, or -1 if a node is unsupported. */
  int analyze(int i) {
    const node::type t = nodes_[i].t;
    if (!supported(t))
      return -1;
    int end = i + 1, need = 1;
    if (detail::arity(t) >= 1) {
      end = analyze(i + 1);
      if (end < 0)
        return -1;
      need = need_[i + 1];
    }
    if (detail::arity(t) == 2) {
      const int r = end;
      end = analyze(r);
      if (end < 0)
        return -1;
      const int l_need = need_[i + 1], r_need = need_[r];
      need = l_need == r_need ? l_need + 1 : std::max(l_need, r_need);
    }
    end_[i] = end;
    need_[i] = need;
    return end;
  }

  static bool supported(node::type t) {
    switch (t) {
    case node::type::variable:
    case node::type::constant:
    case node::type::add:
    case node::type::sub:
    case node::type::mul:
    case node::type::fdim:
    case node::type::abs:
    case node::type::sq:
    case node::type::sqrt:
    case node::type::sin:
    case node::type::cos:
    case node::type::log:
      return true;
    default:
      return false;
    }
  }

  /** Emits the subtree at `i`, with `k` values already live. The higher-need
   * child of a binary node goes first (Sethi-Ullman). */
  void emit(int i, int k) {
    const node &n = nodes_[i];
    if (detail::arity(n.t) == 0) {
      emit_leaf(n, k);
    } else if (detail::arity(n.t) == 1) {
      emit(i + 1, k);
      emit_unary(n.t, k + 1);
    } else {
      const int l = i + 1, r = end_[l];
      const bool swap = need_[r] > need_[l];
      emit(swap ? r : l, k);
      emit(swap ? l : r, k + 1);
      emit_binary(n.t, k + 2, swap);
    }
  }

  void emit_leaf(const node &n, int k) {
    if (n.t == node::type::variable) {
      put(stencils::var[k], n.u.fid * n_rows_ * sizeof(float));
    } else {
      uint32_t bits;
      std::memcpy(&bits, &n.u.val, sizeof(bits));
      put(stencils::constant[k], bits);
    }
  }

  void emit_unary(node::type t, int k) {
    switch (t) {
    case node::type::abs:
      return put(stencils::unary_abs[k], 0);
    case node::type::sq:
      return put(stencils::unary_sq[k], 0);
    case node::type::sqrt:
      return put(stencils::unary_sqrt[k], 0);
    case node::type::sin:
      return put(stencils::call_sin[k], address(sinf));
    case node::type::cos:
      return put(stencils::call_cos[k], address(cosf));
    case node::type::log:
      return put(stencils::call_log[k], address(logf));
    default:
      abort();
    }
  }

  /** `swap` means the second operand was evaluated first, so it sits below
   * the first operand on the stack. */
  void emit_binary(node::type t, int k, bool swap) {
    switch (t) {
    case node::type::add:
      return put(stencils::binary_add[k], 0);
    case node::type::mul:
      return put(stencils::binary_mul[k], 0);
    case node::type::sub:
      return put(swap ? stencils::binary_rsub[k] : stencils::binary_sub[k], 0);
    case node::type::fdim:
      return put(swap ? stencils::binary_rfdim[k] : stencils::binary_fdim[k],
                 0);
    default:
      abort();
    }
  }

  /** Copies a stencil to the end of the code and patches it. */
  void put(const stencil &s, uint64_t value) {
    std::memcpy(at_, s.code, s.size);
    for (const patch *p = s.patches; p != s.patches + s.n_patches; ++p) {
      uint8_t *loc = at_ + p->offset;
      if (p->hole) {
        std::memcpy(loc, &value, sizeof(value));
      } else {
        const int32_t rel = static_cast<int32_t>(
            reinterpret_cast<intptr_t>(rodata_ + p->target) -
            reinterpret_cast<intptr_t>(loc));
        std::memcpy(loc, &rel, sizeof(rel));
      }
    }
    at_ += s.size;
  }

  const node *nodes_;
  uint64_t n_rows_;
  std::vector<int> end_, need_;
  bool ok_;
  uint8_t *at_ = nullptr;
  const uint8_t *rodata_ = nullptr;
};

} // namespace

void execute(const program_t progs, const float *data, float *y_pred,
             const uint64_t n_rows, const uint64_t n_progs) {
  static buffer buf;
  if (n_rows < BLOCK)
    return execute_kernel(progs, data, y_pred, n_rows, n_progs);
  for (uint64_t pid = 0; pid < n_progs; ++pid) {
    float *y = y_pred + pid * n_rows;
    compiler c(progs[pid], n_rows);
    if (!c.ok()) {
      execute_kernel(progs + pid, data, y, n_rows, 1);
      continue;
    }
    kernel_t kernel = c.compile(buf);
    for (uint64_t row = 0; row + BLOCK <= n_rows; row += BLOCK)
      kernel(data + row, y + row);
    if (n_rows % BLOCK != 0)
      kernel(data + n_rows - BLOCK, y + n_rows - BLOCK);
  }
}

} // namespace jit
} // namespace genetic
