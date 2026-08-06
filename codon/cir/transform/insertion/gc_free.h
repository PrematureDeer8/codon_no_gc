#include "codon/cir/transform/pass.h"
#include "codon/cir/cir.h"
#include "codon/cir/util/visitor.h"
#include "codon/cir/util/irtools.h"
#include "codon/cir/util/matching.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace codon{
namespace ir {
namespace transform {
namespace insertion {

// -----------------------------------------------------------------------------
// 1. The Helper Visitor: Hunts down every return statement in a function
// -----------------------------------------------------------------------------
struct ReturnFinder : public ir::util::Operator {
    std::vector<ir::ReturnInstr*> returns;

    void handle(ir::ReturnInstr *instr) override;
};

struct VarFinder : public ir::util::Operator {
    ir::Var* target_var = nullptr;
    std::unordered_map<ir::SeriesFlow*, ir::AssignInstr*> sf_last_assign;

    void handle(ir::AssignInstr *instr) override;
};

struct AliasGenerator : public ir::util::Operator {
    ir::Module* M;

    ir::BodiedFunc* current_func = nullptr;
    ir::SeriesFlow* current_block = nullptr;
    

    std::unordered_map<ir::SeriesFlow*, std::vector<ir::Var*>> vars_to_free_in_block;
    std::unordered_map<ir::Func*, bool> allocates_memory;
    // generated_aliases are candidates for inserting GC frees
    // these variables are deemed to be in a function's local scope (escape analysis)
    std::unordered_map<ir::Var*, ir::SeriesFlow*> generated_aliases;

    // void handle(ir::ReturnInstr *instr) override;
    void handle(ir::CallInstr *instr) override;
    void nested_instr_handler(ir::CallInstr *instr);
    void handle(ir::ReturnInstr *instr) override;
    void handle(ir::SeriesFlow *flow) override;

};

// struct

// -----------------------------------------------------------------------------
// 2. The Core GC Pass
// -----------------------------------------------------------------------------
class GCFree : public Pass {
private:
    // The memoization cache: remembers if a function returns heap memory
    std::unordered_map<ir::Func*, bool> allocates_memory;
    std::unordered_map<ir::BodiedFunc*, std::vector<ir::ReturnInstr*>> return_statements;

    // TODO: Implement your traceback logic here
    bool tracesToSeqAlloc(ir::Value* val);

    // Recursive analyzer with cycle prevention
    bool checkFunctionAllocates(ir::Func* func, std::unordered_set<ir::Func*>& visited);
    bool checkVarIsOnHeap(ir::BodiedFunc* func, ir::Var* var, std::unordered_set<ir::Func*>& visited);

    // insert GC free calls
    // void insertGCFree(ir::Value* val);

    void is_rvalue(ir::Value* val);

public:
    static const std::string KEY;
    std::string getKey() const override { return KEY; }

    // -------------------------------------------------------------------------
    // The main entry point called by the PassManager
    // -------------------------------------------------------------------------
    void run(ir::Module *module) override;

};

// Required static definition for the pass key
inline const std::string GCFree::KEY = "gc-free";

} // namespace insertion
}
}
}